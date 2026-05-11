import aiosqlite
import os

DB_PATH = os.path.join(os.path.dirname(__file__), "..", "data", "ai_watch.db")

DEFAULT_CHAT_SYSTEM = (
    "你是 Ai Watch 智能助理，回答简洁，可中文。"
    "用户通过手表语音或文字与你对话；不要编造未发生的录音内容。"
)


async def init_db():
    os.makedirs(os.path.dirname(DB_PATH), exist_ok=True)
    async with aiosqlite.connect(DB_PATH) as db:
        await db.execute(
            """
            CREATE TABLE IF NOT EXISTS schedules (
                id INTEGER PRIMARY KEY AUTOINCREMENT,
                title TEXT NOT NULL,
                start_ts INTEGER NOT NULL,
                end_ts INTEGER,
                remind_before_sec INTEGER DEFAULT 900,
                hourly_reminder INTEGER DEFAULT 0,
                recurrence TEXT DEFAULT '',
                created_at INTEGER DEFAULT (strftime('%s','now'))
            );
            """
        )
        await db.execute(
            """
            CREATE TABLE IF NOT EXISTS recordings (
                id INTEGER PRIMARY KEY AUTOINCREMENT,
                path TEXT NOT NULL,
                device_name TEXT,
                transcript TEXT DEFAULT '',
                created_at INTEGER DEFAULT (strftime('%s','now'))
            );
            """
        )
        await db.execute(
            """
            CREATE TABLE IF NOT EXISTS server_settings (
                k TEXT PRIMARY KEY,
                v TEXT NOT NULL
            );
            """
        )
        await db.execute(
            """
            CREATE TABLE IF NOT EXISTS chat_messages (
                id INTEGER PRIMARY KEY AUTOINCREMENT,
                device_name TEXT DEFAULT '',
                role TEXT NOT NULL,
                content TEXT NOT NULL,
                source TEXT DEFAULT '',
                created_at INTEGER DEFAULT (strftime('%s','now'))
            );
            """
        )
        await db.commit()


async def get_system_prompt() -> str:
    async with aiosqlite.connect(DB_PATH) as conn:
        cur = await conn.execute(
            "SELECT v FROM server_settings WHERE k = ?",
            ("chat_system_prompt",),
        )
        row = await cur.fetchone()
    if row and (row[0] or "").strip():
        return str(row[0]).strip()
    return DEFAULT_CHAT_SYSTEM


async def set_system_prompt(text: str) -> None:
    async with aiosqlite.connect(DB_PATH) as conn:
        await conn.execute(
            """
            INSERT INTO server_settings (k, v) VALUES (?, ?)
            ON CONFLICT(k) DO UPDATE SET v = excluded.v
            """,
            ("chat_system_prompt", text.strip()),
        )
        await conn.commit()


async def clear_system_prompt() -> None:
    async with aiosqlite.connect(DB_PATH) as conn:
        await conn.execute("DELETE FROM server_settings WHERE k = ?", ("chat_system_prompt",))
        await conn.commit()


async def insert_chat_message(
    device_name: str, role: str, content: str, source: str = ""
) -> None:
    async with aiosqlite.connect(DB_PATH) as conn:
        await conn.execute(
            """
            INSERT INTO chat_messages (device_name, role, content, source)
            VALUES (?, ?, ?, ?)
            """,
            (device_name or "", role, content, source or ""),
        )
        await conn.commit()


async def list_chat_messages(limit: int = 200):
    lim = max(1, min(int(limit), 500))
    async with aiosqlite.connect(DB_PATH) as conn:
        conn.row_factory = aiosqlite.Row
        cur = await conn.execute(
            """
            SELECT id, device_name, role, content, source, created_at
            FROM chat_messages
            ORDER BY id DESC
            LIMIT ?
            """,
            (lim,),
        )
        rows = await cur.fetchall()
    return {"items": [dict(r) for r in rows]}
