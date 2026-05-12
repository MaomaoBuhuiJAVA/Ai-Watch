import asyncio
import json
import logging
import os
import time
from datetime import datetime, timedelta, timezone
from typing import Optional

from dotenv import load_dotenv

load_dotenv()

import aiosqlite
import httpx
from fastapi import FastAPI, Header, Request
from fastapi.middleware.cors import CORSMiddleware
from fastapi.responses import FileResponse, JSONResponse, PlainTextResponse, Response

from . import baidu_tts, db, stt, voice_stream
from .deepseek_client import chat_completion
from .recording_service import enrich_recording_after_save, summarize_recording

log = logging.getLogger(__name__)

STATIC_DIR = os.path.join(os.path.dirname(__file__), "..", "static")
DATA_ROOT = os.path.realpath(os.path.join(os.path.dirname(__file__), "..", "data"))

app = FastAPI(title="Ai Watch Server", version="0.1.0")
app.add_middleware(
    CORSMiddleware,
    allow_origins=["*"],
    allow_methods=["*"],
    allow_headers=["*"],
)


@app.on_event("startup")
async def _startup():
    await db.init_db()
    if not (os.getenv("DEEPSEEK_API_KEY") or "").strip():
        log.warning(
            "DEEPSEEK_API_KEY is empty — /api/chat and /api/chat/from_wav return 503 until set in server/.env"
        )


@app.get("/")
async def root():
    return PlainTextResponse(
        "Ai Watch API\n"
        "  GET  /health\n"
        "  GET  /api/time  (北京时间 Unix，供手表 HTTP 校时)\n"
        "  GET  /desk  (服务台网页：历史与系统提示词)\n"
        "  POST /api/chat  (JSON: {\"message\":\"...\"})\n"
        "  POST /api/chat/from_wav  (body: WAV, 语音识别后对话)\n"
        "  POST /api/voice_stream/start | .../chunk | .../finish(save_recording→desk) | .../chat\n"
        "  POST /api/tts  (JSON: {\"text\":\"...\"} → audio/wav，需百度密钥)\n"
        "  POST /api/recordings/upload  (body: WAV)\n"
        "  GET  /api/recordings  (含分类、转写路径、总结 JSON)\n"
        "  GET  /api/recordings/{id}/file?kind=wav|mp3|txt\n"
        "  POST /api/recordings/{id}/summarize  (JSON 可选 prompt_card_slug)\n"
        "  GET/POST/DELETE /api/prompt_cards  (提示词方案卡片)\n"
        "  GET  /docs  (Swagger)\n",
        media_type="text/plain; charset=utf-8",
    )


@app.get("/desk")
async def desk_page():
    path = os.path.join(STATIC_DIR, "desk.html")
    if not os.path.isfile(path):
        return PlainTextResponse("desk.html missing", status_code=404)
    return FileResponse(path, media_type="text/html; charset=utf-8")


@app.get("/favicon.ico")
async def favicon():
    return Response(status_code=204)


@app.get("/health")
async def health():
    return {"ok": True, "ts": int(time.time())}


_BEIJING_TZ = timezone(timedelta(hours=8), name="CST")


@app.get("/api/time")
async def api_time_beijing():
    """手表联网后 GET 此接口，用返回的 unix 秒设置 RTC（与电脑/服务端同一套北京时间）。"""
    now = datetime.now(_BEIJING_TZ)
    return {
        "unix": int(now.timestamp()),
        "timezone": "Asia/Shanghai",
        "beijing_iso": now.isoformat(timespec="seconds"),
    }


@app.post("/api/chat")
async def api_chat(
    request: Request,
    x_device_name: Optional[str] = Header(default=None, alias="X-Device-Name"),
):
    body = await request.json()
    msg = (body.get("message") or "").strip()
    if not msg:
        return JSONResponse({"error": "empty message"}, status_code=400)
    sys_p = await db.get_system_prompt()
    try:
        reply = await chat_completion(msg, system_prompt=sys_p)
    except ValueError as e:
        return JSONResponse({"error": str(e)}, status_code=503)
    dev = x_device_name or ""
    await db.insert_chat_message(dev, "user", msg, "text")
    await db.insert_chat_message(dev, "assistant", reply, "text")
    return PlainTextResponse(reply, media_type="text/plain; charset=utf-8")


@app.post("/api/chat/from_wav")
async def chat_from_wav(
    request: Request,
    x_device_name: Optional[str] = Header(default=None, alias="X-Device-Name"),
):
    raw = await request.body()
    if len(raw) < 1200:
        return JSONResponse(
            {"error": "wav too small", "transcript": "", "reply": ""},
            status_code=400,
        )
    loop = asyncio.get_running_loop()

    def _stt() -> str:
        return stt.transcribe_wav_bytes(raw)

    try:
        transcript = (await loop.run_in_executor(None, _stt)).strip()
    except RuntimeError as e:
        log.warning("STT unavailable: %s", e)
        return JSONResponse(
            {"error": str(e), "transcript": "", "reply": ""},
            status_code=503,
        )
    except Exception as e:
        log.exception("STT failed (see traceback above)")
        return JSONResponse(
            {"error": f"stt failed: {e}", "transcript": "", "reply": ""},
            status_code=500,
        )

    if not transcript:
        return JSONResponse(
            {
                "error": "未识别到语音，请靠近麦克风再说一次",
                "transcript": "",
                "reply": "",
            },
            status_code=400,
        )

    try:
        sys_p = await db.get_system_prompt()
        reply = await chat_completion(transcript, system_prompt=sys_p)
        dev = x_device_name or ""
        await db.insert_chat_message(dev, "user", transcript, "voice")
        await db.insert_chat_message(dev, "assistant", reply, "voice")
        return {"transcript": transcript, "reply": reply}
    except ValueError as e:
        log.warning("DeepSeek not configured: %s", e)
        return JSONResponse(
            {"error": str(e), "transcript": transcript, "reply": ""},
            status_code=503,
        )
    except httpx.HTTPStatusError as e:
        snippet = ""
        try:
            snippet = (e.response.text or "")[:400]
        except Exception:
            pass
        log.exception("DeepSeek HTTP error status=%s", e.response.status_code)
        return JSONResponse(
            {
                "error": f"DeepSeek API HTTP {e.response.status_code}: {snippet or str(e)}",
                "transcript": transcript,
                "reply": "",
            },
            status_code=502,
        )
    except httpx.RequestError as e:
        log.exception("DeepSeek network error")
        return JSONResponse(
            {
                "error": f"DeepSeek 网络错误: {e}",
                "transcript": transcript,
                "reply": "",
            },
            status_code=502,
        )
    except Exception as e:
        log.exception("chat_from_wav failed after STT (DB or LLM)")
        return JSONResponse(
            {
                "error": f"reply failed: {e}",
                "transcript": transcript,
                "reply": "",
            },
            status_code=500,
        )


@app.post("/api/voice_stream/start")
async def voice_stream_start_ep():
    return await voice_stream.voice_stream_start()


@app.post("/api/voice_stream/{sid}/chunk")
async def voice_stream_chunk_ep(sid: str, request: Request):
    body = await request.body()
    return await voice_stream.voice_stream_chunk(sid, body)


@app.post("/api/voice_stream/{sid}/finish")
async def voice_stream_finish_ep(
    sid: str,
    request: Request,
    x_device_name: Optional[str] = Header(default=None, alias="X-Device-Name"),
):
    try:
        body = await request.json()
    except Exception:
        body = {}
    do_chat = bool(body.get("chat", False))
    save_recording = bool(body.get("save_recording", False))
    return await voice_stream.voice_stream_finish(
        sid, do_chat, dev_name=x_device_name or "", save_recording=save_recording
    )


@app.post("/api/voice_stream/{sid}/chat")
async def voice_stream_chat_ep(
    sid: str,
    x_device_name: Optional[str] = Header(default=None, alias="X-Device-Name"),
):
    return await voice_stream.voice_stream_chat(sid, x_device_name=x_device_name)


@app.post("/api/tts")
async def api_tts(request: Request):
    """文本转语音：返回 WAV（16k mono PCM），手表直接播放。需配置 BAIDU_API_KEY + BAIDU_SECRET_KEY。"""
    body = await request.json()
    text = (body.get("text") or "").strip()
    if not text:
        return JSONResponse({"error": "empty text"}, status_code=400)
    if not os.getenv("BAIDU_API_KEY", "").strip() or not os.getenv("BAIDU_SECRET_KEY", "").strip():
        return JSONResponse(
            {"error": "TTS requires BAIDU_API_KEY and BAIDU_SECRET_KEY in server/.env"},
            status_code=503,
        )

    loop = asyncio.get_running_loop()

    def _run():
        return baidu_tts.synthesize_wav(text)

    try:
        wav = await loop.run_in_executor(None, _run)
    except Exception as e:
        log.exception("TTS failed")
        return JSONResponse({"error": str(e)}, status_code=500)

    if not wav or len(wav) < 100:
        return JSONResponse({"error": "empty audio"}, status_code=500)
    return Response(content=wav, media_type="audio/wav")


@app.get("/api/chat/history")
async def chat_history(limit: int = 200):
    return await db.list_chat_messages(limit)


@app.get("/api/settings/chat_system_prompt")
async def get_chat_system_prompt():
    return {"value": await db.get_system_prompt()}


@app.post("/api/settings/chat_system_prompt")
async def set_chat_system_prompt(request: Request):
    body = await request.json()
    if "value" not in body:
        return JSONResponse({"error": "missing value"}, status_code=400)
    val = body.get("value")
    if not isinstance(val, str):
        return JSONResponse({"error": "value must be string"}, status_code=400)
    if not val.strip():
        await db.clear_system_prompt()
    else:
        await db.set_system_prompt(val)
    return {"ok": True, "value": await db.get_system_prompt()}


@app.post("/api/recordings/upload")
async def upload_recording(
    request: Request,
    x_device_name: Optional[str] = Header(default=None, alias="X-Device-Name"),
):
    raw = await request.body()
    if len(raw) < 44:
        return JSONResponse({"error": "body too small for WAV"}, status_code=400)
    wav_dir = os.path.join(os.path.dirname(__file__), "..", "data", "wav")
    os.makedirs(wav_dir, exist_ok=True)
    name = f"rec_{int(time.time())}.wav"
    path = os.path.join(wav_dir, name)
    with open(path, "wb") as f:
        f.write(raw)
    async with aiosqlite.connect(db.DB_PATH) as conn:
        await conn.execute(
            "INSERT INTO recordings (path, device_name) VALUES (?, ?)",
            (path, x_device_name or ""),
        )
        await conn.commit()
        cur = await conn.execute("SELECT last_insert_rowid()")
        row = await cur.fetchone()
        rid = row[0] if row else None
    out = {"id": rid, "saved": name}
    if rid is not None:
        try:
            asyncio.get_running_loop().create_task(enrich_recording_after_save(rid, path, x_device_name or ""))
        except RuntimeError:
            pass
    return out


@app.get("/api/recordings")
async def list_recordings_api(limit: int = 80):
    return await db.list_recordings(limit)


def _file_under_data(path: Optional[str]) -> Optional[str]:
    if not path or not os.path.isfile(path):
        return None
    rp = os.path.realpath(path)
    if not rp.startswith(DATA_ROOT + os.sep):
        return None
    return rp


@app.get("/api/recordings/{rid}/file")
async def recording_file(rid: int, kind: str = "wav"):
    row = await db.get_recording_row(rid)
    if not row:
        return JSONResponse({"error": "not found"}, status_code=404)
    kind = (kind or "wav").lower().strip()
    key = {"wav": "path", "mp3": "mp3_path", "txt": "txt_path"}.get(kind)
    if not key:
        return JSONResponse({"error": "kind must be wav|mp3|txt"}, status_code=400)
    p = _file_under_data(row.get(key) or "")
    if not p:
        return JSONResponse({"error": "file not ready or unsupported"}, status_code=404)
    media = {"wav": "audio/wav", "mp3": "audio/mpeg", "txt": "text/plain; charset=utf-8"}[kind]
    return FileResponse(p, media_type=media, filename=os.path.basename(p))


@app.post("/api/recordings/{rid}/summarize")
async def recording_summarize(rid: int, request: Request):
    extra = None
    try:
        body = await request.json()
        slug = (body.get("prompt_card_slug") or "").strip()
        if slug:
            extra = await db.get_prompt_card_body(slug)
    except Exception:
        pass
    out = await summarize_recording(rid, extra_system=extra)
    if out.get("error"):
        return JSONResponse(out, status_code=400 if "not found" in out["error"] else 503)
    return out


@app.get("/api/prompt_cards")
async def prompt_cards_list():
    return await db.list_prompt_cards()


@app.post("/api/prompt_cards")
async def prompt_cards_upsert(request: Request):
    body = await request.json()
    slug = (body.get("slug") or "").strip()
    title = (body.get("title") or "").strip()
    pbody = body.get("body")
    if not isinstance(pbody, str):
        pbody = ""
    sort_order = int(body.get("sort_order", 0))
    try:
        await db.upsert_prompt_card(slug, title, pbody, sort_order)
    except ValueError as e:
        return JSONResponse({"error": str(e)}, status_code=400)
    return {"ok": True}


@app.delete("/api/prompt_cards/{slug}")
async def prompt_cards_delete(slug: str):
    if slug in ("default",):
        return JSONResponse({"error": "cannot delete built-in default"}, status_code=400)
    ok = await db.delete_prompt_card(slug)
    return {"ok": ok}


@app.post("/api/schedule/plan")
async def plan_schedule(request: Request):
    body = await request.json()
    goal = (body.get("goal") or "").strip()
    if not goal:
        return JSONResponse({"error": "empty goal"}, status_code=400)
    prompt = (
        "根据用户需求生成最多 5 条日程，每条含 title, start_ts(Unix秒), end_ts, "
        "remind_before_sec(默认900), hourly_reminder(0/1), recurrence(空或描述)。"
        "只输出 JSON 数组，不要 Markdown。\n需求：\n"
        + goal
    )
    try:
        text = await chat_completion(prompt, system_prompt="只输出合法 JSON 数组。")
    except ValueError as e:
        return JSONResponse({"error": str(e), "items": []}, status_code=503)
    try:
        items = json.loads(text)
    except json.JSONDecodeError:
        return {"raw": text, "items": []}
    return {"items": items}


@app.post("/api/schedule/batch")
async def schedule_batch(request: Request):
    items = (await request.json()).get("items") or []
    async with aiosqlite.connect(db.DB_PATH) as conn:
        for it in items:
            end_ts = it.get("end_ts")
            await conn.execute(
                """
                INSERT INTO schedules (title, start_ts, end_ts, remind_before_sec, hourly_reminder, recurrence)
                VALUES (?, ?, ?, ?, ?, ?)
                """,
                (
                    it.get("title", "未命名"),
                    int(it.get("start_ts", 0)),
                    int(end_ts) if end_ts is not None else None,
                    int(it.get("remind_before_sec", 900)),
                    int(it.get("hourly_reminder", 0)),
                    str(it.get("recurrence", "")),
                ),
            )
        await conn.commit()
    return {"ok": True, "count": len(items)}


@app.get("/api/schedule")
async def schedule_list():
    async with aiosqlite.connect(db.DB_PATH) as conn:
        conn.row_factory = aiosqlite.Row
        cur = await conn.execute(
            "SELECT id, title, start_ts, end_ts, remind_before_sec, hourly_reminder, recurrence FROM schedules ORDER BY start_ts ASC LIMIT 100"
        )
        rows = await cur.fetchall()
    return {"items": [dict(r) for r in rows]}


@app.post("/api/recordings/{rid}/transcribe_stub")
async def transcribe_stub(rid: int):
    async with aiosqlite.connect(db.DB_PATH) as conn:
        await conn.execute(
            "UPDATE recordings SET transcript = ? WHERE id = ?",
            ("（转写占位）", rid),
        )
        await conn.commit()
    return {"ok": True, "id": rid}


@app.post("/api/search")
async def search_recordings(request: Request):
    q = ((await request.json()).get("q") or "").strip()
    if not q:
        return {"items": [], "answer": ""}
    sys_p = await db.get_system_prompt()
    try:
        plan = await chat_completion(
            f"用户要在录音文字里找：{q}。当前无真实转写，请说明如何检索并给出后续接入建议。简短。",
            system_prompt=sys_p,
        )
    except ValueError as e:
        return JSONResponse({"answer": "", "items": [], "error": str(e)}, status_code=503)
    return {"answer": plan, "items": []}
