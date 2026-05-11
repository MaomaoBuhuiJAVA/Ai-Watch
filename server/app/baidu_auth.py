"""百度 OAuth access_token（语音识别 / 语音合成共用）。"""

from __future__ import annotations

import os
import time
from typing import Optional

import httpx

_TOKEN: Optional[str] = None
_TOKEN_EXPIRES_AT: float = 0.0


def get_access_token() -> str:
    global _TOKEN, _TOKEN_EXPIRES_AT
    api_key = os.getenv("BAIDU_API_KEY", "").strip()
    secret = os.getenv("BAIDU_SECRET_KEY", "").strip()
    if not api_key or not secret:
        raise RuntimeError("BAIDU_API_KEY / BAIDU_SECRET_KEY 未配置（写入 server/.env）")

    now = time.time()
    if _TOKEN and now < _TOKEN_EXPIRES_AT - 120:
        return _TOKEN

    url = "https://aip.baidubce.com/oauth/2.0/token"
    params = {
        "grant_type": "client_credentials",
        "client_id": api_key,
        "client_secret": secret,
    }
    with httpx.Client(timeout=30.0) as client:
        r = client.post(url, params=params)
        r.raise_for_status()
        data = r.json()

    if "access_token" not in data:
        raise RuntimeError(f"Baidu OAuth 失败: {data}")

    _TOKEN = str(data["access_token"])
    expires_in = int(data.get("expires_in", 2592000))
    _TOKEN_EXPIRES_AT = now + expires_in
    return _TOKEN
