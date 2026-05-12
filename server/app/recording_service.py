"""录音入库后：转写、TXT/MP3 打包、自动分类、总结（DeepSeek）。"""

from __future__ import annotations

import asyncio
import json
import logging
import os
import re
import shutil
import subprocess
from typing import Any, Optional

import aiosqlite

from . import db, stt
from .deepseek_client import chat_completion

log = logging.getLogger(__name__)

BUNDLE_ROOT = os.path.join(os.path.dirname(__file__), "..", "data", "bundles")
CATEGORIES = ("work_plan", "voice_review", "other")


def _safe_bundle_dir(rid: int) -> str:
    d = os.path.join(BUNDLE_ROOT, str(int(rid)))
    os.makedirs(d, exist_ok=True)
    return d


def _wav_to_mp3(wav_path: str, mp3_path: str) -> bool:
    ffmpeg = shutil.which("ffmpeg")
    if not ffmpeg:
        log.warning("ffmpeg not in PATH — skip MP3; install ffmpeg for MP3 export")
        return False
    try:
        subprocess.run(
            [
                ffmpeg,
                "-y",
                "-i",
                wav_path,
                "-codec:a",
                "libmp3lame",
                "-qscale:a",
                "4",
                mp3_path,
            ],
            check=True,
            capture_output=True,
            timeout=600,
        )
        return os.path.isfile(mp3_path) and os.path.getsize(mp3_path) > 0
    except (subprocess.CalledProcessError, OSError, subprocess.TimeoutExpired) as e:
        log.warning("MP3 encode failed: %s", e)
        return False


def _extract_json_object(text: str) -> Optional[dict[str, Any]]:
    if not text or not text.strip():
        return None
    t = text.strip()
    try:
        return json.loads(t)
    except json.JSONDecodeError:
        pass
    m = re.search(r"\{[\s\S]*\}", t)
    if m:
        try:
            return json.loads(m.group(0))
        except json.JSONDecodeError:
            return None
    return None


async def _classify_category(transcript: str) -> str:
    if not (transcript or "").strip():
        return "other"
    try:
        sys_p = (
            "你是分类器。只输出合法 JSON，不要 Markdown，不要解释。"
            '格式：{"category":"work_plan"|"voice_review"|"other"}。\n'
            "work_plan=工作计划、任务、会议安排、待办；voice_review=复盘、总结、反思、学习心得；"
            "other=闲聊、杂项。"
        )
        raw = await chat_completion(
            f"对下列录音转写分类：\n{(transcript or '')[:6000]}",
            system_prompt=sys_p,
        )
        data = _extract_json_object(raw) or {}
        cat = (data.get("category") or "other").strip().lower()
        if cat in CATEGORIES:
            return cat
        if "work" in cat:
            return "work_plan"
        if "voice" in cat or "review" in cat or "复盘" in raw:
            return "voice_review"
        return "other"
    except Exception as e:
        log.warning("classify skip: %s", e)
        return "other"


async def enrich_recording_after_save(recording_id: int, wav_path: str, device_name: str = "") -> None:
    """后台：读 WAV → 转写 → TXT+MP3 → 更新库 → 自动分类。"""
    rid = int(recording_id)
    if not os.path.isfile(wav_path):
        log.error("enrich: missing wav %s", wav_path)
        return
    try:
        with open(wav_path, "rb") as f:
            wav_bytes = f.read()
    except OSError as e:
        log.error("enrich read wav: %s", e)
        return

    loop = asyncio.get_running_loop()

    def _stt() -> str:
        try:
            return stt.transcribe_wav_bytes(wav_bytes).strip()
        except Exception as ex:
            log.exception("STT failed rid=%s", rid)
            return ""

    transcript = await loop.run_in_executor(None, _stt)
    bundle = _safe_bundle_dir(rid)
    txt_path = os.path.join(bundle, "transcript.txt")
    mp3_path = os.path.join(bundle, "audio.mp3")
    try:
        with open(txt_path, "w", encoding="utf-8") as f:
            f.write(transcript or "")
    except OSError as e:
        log.error("write txt: %s", e)
        txt_path = ""

    mp3_ok = await loop.run_in_executor(None, lambda: _wav_to_mp3(wav_path, mp3_path))
    if not mp3_ok:
        mp3_path = ""

    category = await _classify_category(transcript)

    async with aiosqlite.connect(db.DB_PATH) as conn:
        await conn.execute(
            """
            UPDATE recordings
            SET transcript = ?, txt_path = ?, mp3_path = ?, category = ?
            WHERE id = ?
            """,
            (transcript, txt_path or "", mp3_path or "", category, rid),
        )
        await conn.commit()
    log.info("recording %s enriched: category=%s transcript_len=%s mp3=%s", rid, category, len(transcript), bool(mp3_path))


async def summarize_recording(
    recording_id: int,
    extra_system: Optional[str] = None,
) -> dict[str, Any]:
    """生成标题、要点高亮、总结报告；写入 summary_json。"""
    rid = int(recording_id)
    async with aiosqlite.connect(db.DB_PATH) as conn:
        conn.row_factory = aiosqlite.Row
        cur = await conn.execute(
            "SELECT id, transcript, path FROM recordings WHERE id = ?",
            (rid,),
        )
        row = await cur.fetchone()
    if not row:
        return {"error": "not found"}
    transcript = (row["transcript"] or "").strip()
    if not transcript:
        return {"error": "empty transcript; wait for auto transcribe or retry later"}

    sys_p = extra_system or (
        "你是办公助手。根据录音转写生成结构化总结，只输出一个 JSON 对象，不要 Markdown。"
        '键：title（≤30字标题）, highlights（3～8条关键短语，须能在原文中字面出现）, '
        'report（200～600字正文，中文，分段用\\n）。'
    )
    try:
        raw = await chat_completion(
            f"录音转写如下：\n{transcript[:12000]}",
            system_prompt=sys_p,
        )
    except ValueError as e:
        return {"error": str(e)}

    data = _extract_json_object(raw)
    if not data:
        return {"error": "model did not return JSON", "raw": raw[:800]}

    title = str(data.get("title") or "录音摘要").strip()
    highlights = data.get("highlights")
    if not isinstance(highlights, list):
        highlights = []
    highlights = [str(x).strip() for x in highlights if str(x).strip()][:12]
    report = str(data.get("report") or "").strip()

    summary = {
        "title": title,
        "highlights": highlights,
        "report": report,
    }
    blob = json.dumps(summary, ensure_ascii=False)

    async with aiosqlite.connect(db.DB_PATH) as conn:
        await conn.execute(
            "UPDATE recordings SET summary_json = ? WHERE id = ?",
            (blob, rid),
        )
        await conn.commit()
    return {"ok": True, "summary": summary}
