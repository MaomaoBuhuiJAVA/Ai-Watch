import json
import os
import time
from typing import Optional

import aiosqlite
from dotenv import load_dotenv
from fastapi import FastAPI, Header, Request
from fastapi.middleware.cors import CORSMiddleware
from fastapi.responses import JSONResponse, PlainTextResponse

from . import db
from .deepseek_client import chat_completion

load_dotenv()

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


@app.get("/health")
async def health():
    return {"ok": True, "ts": int(time.time())}


@app.post("/api/chat")
async def api_chat(request: Request):
    body = await request.json()
    msg = (body.get("message") or "").strip()
    if not msg:
        return JSONResponse({"error": "empty message"}, status_code=400)
    reply = await chat_completion(msg)
    return PlainTextResponse(reply, media_type="text/plain; charset=utf-8")


@app.post("/api/recordings/upload")
async def upload_recording(
    request: Request,
    x_device_name: Optional[str] = Header(default=None, alias="X-Device-Name"),
):
    raw = await request.body()
    if len(raw) < 1000:
        return JSONResponse({"error": "file too small"}, status_code=400)
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
    return {"id": rid, "saved": name}


@app.get("/api/recordings")
async def list_recordings():
    async with aiosqlite.connect(db.DB_PATH) as conn:
        conn.row_factory = aiosqlite.Row
        cur = await conn.execute(
            "SELECT id, path, device_name, transcript, created_at FROM recordings ORDER BY id DESC LIMIT 50"
        )
        rows = await cur.fetchall()
    return {"items": [dict(r) for r in rows]}


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
    text = await chat_completion(prompt, system_prompt="只输出合法 JSON 数组。")
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
    plan = await chat_completion(
        f"用户要在录音文字里找：{q}。当前无真实转写，请说明如何检索并给出后续接入建议。简短。",
    )
    return {"answer": plan, "items": []}
