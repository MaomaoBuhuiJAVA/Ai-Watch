"""百度语音合成（REST text2audio），返回 WAV 字节流。

需 BAIDU_API_KEY / BAIDU_SECRET_KEY；文档：https://ai.baidu.com/ai-doc/SPEECH/Gk38y8lzk
"""

from __future__ import annotations

import json
import os

import httpx

from .baidu_auth import get_access_token

TTS_URL = "https://tsn.baidu.com/text2audio"


def synthesize_wav(text: str) -> bytes:
    text = (text or "").strip()
    if not text:
        return b""

    max_bytes = int(os.getenv("BAIDU_TTS_MAX_BYTES", "900"))
    raw = text.encode("utf-8")
    if len(raw) > max_bytes:
        raw = raw[:max_bytes]
        text = raw.decode("utf-8", errors="ignore")

    token = get_access_token()
    cuid = os.getenv("BAIDU_ASR_CUID", "ai-watch-pc").strip() or "ai-watch-pc"

    form = {
        "tex": text,
        "tok": token,
        "cuid": cuid,
        "ctp": "1",
        "lan": "zh",
        "spd": os.getenv("BAIDU_TTS_SPD", "5").strip() or "5",
        "pit": os.getenv("BAIDU_TTS_PIT", "5").strip() or "5",
        "vol": os.getenv("BAIDU_TTS_VOL", "10").strip() or "10",
        "per": os.getenv("BAIDU_TTS_PER", "0").strip() or "0",
        "aue": "6",
    }

    with httpx.Client(timeout=60.0) as client:
        r = client.post(
            TTS_URL,
            data=form,
            headers={"Content-Type": "application/x-www-form-urlencoded"},
        )
        r.raise_for_status()
        data = r.content

    if len(data) >= 1 and data[0] == ord("{"):
        try:
            err = json.loads(data.decode("utf-8"))
        except json.JSONDecodeError:
            err = {}
        raise RuntimeError(f"Baidu TTS error: {err}")

    if len(data) < 100:
        raise RuntimeError(f"Baidu TTS response too short: {len(data)} bytes")

    return data
