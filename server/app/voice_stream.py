"""PCM 流式上传会话：手表边录边 POST chunk，结束后 seal 或 chat，突破单次 body 内存上限。"""

from __future__ import annotations

import asyncio
import io
import os
import re
import time
import uuid
import wave
from typing import Optional

import aiosqlite
import httpx

from . import db, stt
from .deepseek_client import chat_completion

_SID_RE = re.compile(
    r"^[0-9a-f]{8}-[0-9a-f]{4}-4[0-9a-f]{3}-[89ab][0-9a-f]{3}-[0-9a-f]{12}$",
    re.I,
)

VOICE_TMP = os.path.join(os.path.dirname(__file__), "..", "data", "voice_tmp")
MAX_PCM_PER_SESSION = int(os.getenv("VOICE_STREAM_MAX_BYTES", str(32 * 1024 * 1024)))  # 默认 32MB


def _sid_ok(sid: str) -> bool:
    return bool(sid and _SID_RE.fullmatch(sid))


def _pcm_path(sid: str) -> str:
    return os.path.join(VOICE_TMP, f"{sid}.pcm")


def pcm16_to_wav(pcm: bytes, sample_rate: int = 16000) -> bytes:
    bio = io.BytesIO()
    with wave.open(bio, "wb") as wf:
        wf.setnchannels(1)
        wf.setsampwidth(2)
        wf.setframerate(sample_rate)
        wf.writeframes(pcm)
    return bio.getvalue()


async def voice_stream_start() -> dict:
    os.makedirs(VOICE_TMP, exist_ok=True)
    sid = str(uuid.uuid4())
    path = _pcm_path(sid)
    with open(path, "wb"):
        pass
    return {"session_id": sid}


async def voice_stream_chunk(sid: str, body: bytes) -> dict:
    if not _sid_ok(sid):
        return {"error": "bad session_id"}
    path = _pcm_path(sid)
    if not os.path.isfile(path):
        return {"error": "unknown session"}
    cur = os.path.getsize(path)
    if cur + len(body) > MAX_PCM_PER_SESSION:
        return {"error": "session too large"}
    with open(path, "ab") as f:
        f.write(body)
    return {"ok": True, "total": os.path.getsize(path)}


async def _save_stream_pcm_as_recording(pcm: bytes, dev_name: str) -> dict:
    """将流式会话累积的 PCM 写成 WAV 并登记到 recordings（与 /api/recordings/upload 一致）。"""
    min_pcm = int(os.getenv("VOICE_STREAM_RECORDING_MIN_PCM", "100"))
    if len(pcm) < min_pcm:
        return {"error": "pcm too short", "pcm_bytes": len(pcm)}
    wav_dir = os.path.join(os.path.dirname(__file__), "..", "data", "wav")
    os.makedirs(wav_dir, exist_ok=True)
    name = f"rec_{int(time.time())}.wav"
    path = os.path.join(wav_dir, name)
    wav = pcm16_to_wav(pcm)
    try:
        with open(path, "wb") as f:
            f.write(wav)
    except OSError as e:
        return {"error": str(e), "pcm_bytes": len(pcm)}
    async with aiosqlite.connect(db.DB_PATH) as conn:
        await conn.execute(
            "INSERT INTO recordings (path, device_name) VALUES (?, ?)",
            (path, dev_name or ""),
        )
        await conn.commit()
        cur = await conn.execute("SELECT last_insert_rowid()")
        row = await cur.fetchone()
        rid = row[0] if row else None
    out = {"ok": True, "id": rid, "saved": name, "pcm_bytes": len(pcm)}
    if rid is not None:
        try:
            from .recording_service import enrich_recording_after_save

            asyncio.get_running_loop().create_task(enrich_recording_after_save(rid, path, dev_name or ""))
        except RuntimeError:
            pass
    return out


async def voice_stream_finish(
    sid: str, do_chat: bool, dev_name: str = "", save_recording: bool = False
) -> dict:
    """结束录音：do_chat=True 同场 STT+对话；save_recording=True 写成服务台 WAV（与 do_chat 互斥）；否则仅封存 .pcm。"""
    if not _sid_ok(sid):
        return {"error": "bad session_id"}
    path = _pcm_path(sid)
    if not os.path.isfile(path):
        return {"error": "unknown session"}
    pcm = b""
    try:
        with open(path, "rb") as f:
            pcm = f.read()
    except OSError as e:
        return {"error": str(e)}

    if len(pcm) < 64:
        try:
            os.remove(path)
        except OSError:
            pass
        return {"error": "pcm empty", "pcm_bytes": len(pcm)}

    if do_chat and len(pcm) < 2000:
        try:
            os.remove(path)
        except OSError:
            pass
        return {"error": "pcm too short for speech", "pcm_bytes": len(pcm)}

    if do_chat:
        wav = pcm16_to_wav(pcm)
        try:
            os.remove(path)
        except OSError:
            pass
        return await _stt_chat_from_wav_bytes(wav, dev_name=dev_name)

    if save_recording:
        out = await _save_stream_pcm_as_recording(pcm, dev_name)
        try:
            os.remove(path)
        except OSError:
            pass
        return out

    # 仅封存：保留 .pcm，客户端可随后 POST /chat
    return {"ok": True, "pcm_bytes": len(pcm), "session_id": sid}


async def voice_stream_chat(
    sid: str,
    x_device_name: Optional[str] = None,
) -> dict:
    if not _sid_ok(sid):
        return {"error": "bad session_id"}
    path = _pcm_path(sid)
    if not os.path.isfile(path):
        return {"error": "unknown session or already chatted"}
    with open(path, "rb") as f:
        pcm = f.read()
    try:
        os.remove(path)
    except OSError:
        pass
    if len(pcm) < 2000:
        return {"error": "pcm too short", "transcript": "", "reply": ""}
    wav = pcm16_to_wav(pcm)
    return await _stt_chat_from_wav_bytes(wav, dev_name=x_device_name or "")


async def _stt_chat_from_wav_bytes(wav: bytes, dev_name: str) -> dict:
    import asyncio

    loop = asyncio.get_running_loop()

    def _stt() -> str:
        return stt.transcribe_wav_bytes(wav)

    try:
        transcript = (await loop.run_in_executor(None, _stt)).strip()
    except RuntimeError as e:
        return {"error": str(e), "transcript": "", "reply": ""}
    except Exception as e:
        return {"error": f"stt failed: {e}", "transcript": "", "reply": ""}

    if not transcript:
        return {
            "error": "未识别到语音，请靠近麦克风再说一次",
            "transcript": "",
            "reply": "",
        }

    try:
        sys_p = await db.get_system_prompt()
        reply = await chat_completion(transcript, system_prompt=sys_p)
        await db.insert_chat_message(dev_name, "user", transcript, "voice")
        await db.insert_chat_message(dev_name, "assistant", reply, "voice")
        return {"transcript": transcript, "reply": reply}
    except ValueError as e:
        return {"error": str(e), "transcript": transcript, "reply": ""}
    except httpx.HTTPStatusError as e:
        snippet = ""
        try:
            snippet = (e.response.text or "")[:400]
        except Exception:
            pass
        return {
            "error": f"DeepSeek API HTTP {e.response.status_code}: {snippet or str(e)}",
            "transcript": transcript,
            "reply": "",
        }
    except httpx.RequestError as e:
        return {
            "error": f"DeepSeek 网络错误: {e}",
            "transcript": transcript,
            "reply": "",
        }
    except Exception as e:
        return {"error": f"reply failed: {e}", "transcript": transcript, "reply": ""}
