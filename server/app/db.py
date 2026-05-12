import aiosqlite
import os
from typing import Optional

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
        await db.execute(
            """
            CREATE TABLE IF NOT EXISTS prompt_cards (
                id INTEGER PRIMARY KEY AUTOINCREMENT,
                slug TEXT UNIQUE NOT NULL,
                title TEXT NOT NULL,
                body TEXT NOT NULL,
                sort_order INTEGER DEFAULT 0,
                created_at INTEGER DEFAULT (strftime('%s','now'))
            );
            """
        )
        await _migrate_recordings_columns(db)
        await _seed_prompt_cards_if_empty(db)
        await db.commit()


async def _migrate_recordings_columns(db: aiosqlite.Connection) -> None:
    cur = await db.execute("PRAGMA table_info(recordings)")
    cols = {str(r[1]) for r in await cur.fetchall()}
    alters: list[str] = []
    if "category" not in cols:
        alters.append("ALTER TABLE recordings ADD COLUMN category TEXT DEFAULT 'other'")
    if "txt_path" not in cols:
        alters.append("ALTER TABLE recordings ADD COLUMN txt_path TEXT DEFAULT ''")
    if "mp3_path" not in cols:
        alters.append("ALTER TABLE recordings ADD COLUMN mp3_path TEXT DEFAULT ''")
    if "summary_json" not in cols:
        alters.append("ALTER TABLE recordings ADD COLUMN summary_json TEXT DEFAULT ''")
    for sql in alters:
        await db.execute(sql)


async def _seed_prompt_cards_if_empty(db: aiosqlite.Connection) -> None:
    cur = await db.execute("SELECT COUNT(*) FROM prompt_cards")
    row = await cur.fetchone()
    if row and int(row[0]) > 0:
        return
    seeds = [
        (
            "default",
            "默认对话",
            DEFAULT_CHAT_SYSTEM,
            0,
        ),
        (
            "summary",
            "录音总结",
            "你是总结助手。根据用户提供的转写文本生成 JSON：title、highlights（原文可匹配的短语）、report。只输出 JSON。",
            1,
        ),
        (
            "work",
            "工作计划提炼",
            "你专注工作计划。从转写中提取可执行项、时间节点；输出 JSON：title、highlights、report。只输出 JSON。",
            2,
        ),
    ]
    for slug, title, body, so in seeds:
        await db.execute(
            """
            INSERT OR IGNORE INTO prompt_cards (slug, title, body, sort_order)
            VALUES (?, ?, ?, ?)
            """,
            (slug, title, body, so),
        )


async def list_prompt_cards():
    async with aiosqlite.connect(DB_PATH) as conn:
        conn.row_factory = aiosqlite.Row
        cur = await conn.execute(
            "SELECT id, slug, title, body, sort_order, created_at FROM prompt_cards ORDER BY sort_order ASC, id ASC"
        )
        rows = await cur.fetchall()
    return {"items": [dict(r) for r in rows]}


async def upsert_prompt_card(slug: str, title: str, body: str, sort_order: int = 0) -> None:
    slug = (slug or "").strip()
    if not slug or not title.strip():
        raise ValueError("slug and title required")
    async with aiosqlite.connect(DB_PATH) as conn:
        await conn.execute(
            """
            INSERT INTO prompt_cards (slug, title, body, sort_order)
            VALUES (?, ?, ?, ?)
            ON CONFLICT(slug) DO UPDATE SET
                title = excluded.title,
                body = excluded.body,
                sort_order = excluded.sort_order
            """,
            (slug, title.strip(), (body or "").strip(), int(sort_order)),
        )
        await conn.commit()


async def delete_prompt_card(slug: str) -> bool:
    async with aiosqlite.connect(DB_PATH) as conn:
        cur = await conn.execute("DELETE FROM prompt_cards WHERE slug = ?", (slug,))
        await conn.commit()
        return cur.rowcount > 0


async def get_prompt_card_body(slug: str) -> Optional[str]:
    async with aiosqlite.connect(DB_PATH) as conn:
        cur = await conn.execute("SELECT body FROM prompt_cards WHERE slug = ?", (slug,))
        row = await cur.fetchone()
    if not row:
        return None
    return str(row[0]) if row[0] else None


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


async def list_recordings(limit: int = 80):
    lim = max(1, min(int(limit), 200))
    async with aiosqlite.connect(DB_PATH) as conn:
        conn.row_factory = aiosqlite.Row
        cur = await conn.execute(
            """
            SELECT id, path, device_name, transcript, txt_path, mp3_path, category,
                   summary_json, created_at
            FROM recordings
            ORDER BY id DESC
            LIMIT ?
            """,
            (lim,),
        )
        rows = await cur.fetchall()
    return {"items": [dict(r) for r in rows]}


async def get_recording_row(rid: int) -> Optional[dict]:
    async with aiosqlite.connect(DB_PATH) as conn:
        conn.row_factory = aiosqlite.Row
        cur = await conn.execute(
            """
            SELECT id, path, device_name, transcript, txt_path, mp3_path, category,
                   summary_json, created_at
            FROM recordings WHERE id = ?
            """,
            (int(rid),),
        )
        row = await cur.fetchone()
    return dict(row) if row else None
