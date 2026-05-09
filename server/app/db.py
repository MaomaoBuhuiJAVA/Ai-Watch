import aiosqlite
import os

DB_PATH = os.path.join(os.path.dirname(__file__), "..", "data", "ai_watch.db")


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
        await db.commit()
