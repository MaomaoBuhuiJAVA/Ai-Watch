import os
from typing import Optional

import httpx

BASE = os.getenv("DEEPSEEK_BASE_URL", "https://api.deepseek.com").rstrip("/")
KEY = os.getenv("DEEPSEEK_API_KEY", "")
MODEL = os.getenv("DEEPSEEK_MODEL", "deepseek-chat")


async def chat_completion(user_text: str, system_prompt: Optional[str] = None) -> str:
    if not KEY:
        # 无密钥时仅回显用户原文（不写服务台/手表可见的长说明）
        return (user_text or "").strip()
    system_prompt = system_prompt or (
        "你是 Ai Watch 智能助理，回答简洁，可中文。"
        "用户设备用于日程、录音复盘与隐私控制；不要编造未发生的录音内容。"
    )
    url = f"{BASE}/v1/chat/completions"
    payload = {
        "model": MODEL,
        "messages": [
            {"role": "system", "content": system_prompt},
            {"role": "user", "content": user_text},
        ],
        "temperature": 0.6,
    }
    headers = {"Authorization": f"Bearer {KEY}", "Content-Type": "application/json"}
    async with httpx.AsyncClient(timeout=120.0) as client:
        r = await client.post(url, json=payload, headers=headers)
        r.raise_for_status()
        data = r.json()
        return data["choices"][0]["message"]["content"].strip()
