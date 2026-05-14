import os
from typing import Optional

import httpx

# 注意：不在模块 import 时读 os.getenv — main.py 在 import 本模块后才 load_dotenv()，
# 否则 DEEPSEEK_API_KEY 会一直为空，表现为回显用户话或 503。


async def chat_completion(user_text: str, system_prompt: Optional[str] = None) -> str:
    key = (os.getenv("DEEPSEEK_API_KEY") or "").strip()
    if not key:
        raise ValueError(
            "未配置 DeepSeek：在 server/.env 中设置 DEEPSEEK_API_KEY（https://platform.deepseek.com/api_keys ）"
        )
    base = (os.getenv("DEEPSEEK_BASE_URL") or "https://api.deepseek.com").rstrip("/")
    model = (os.getenv("DEEPSEEK_MODEL") or "deepseek-chat").strip() or "deepseek-chat"
    system_prompt = system_prompt or (
        "你是 智能手表 智能助理，回答简洁，可中文。"
        "用户设备用于日程、录音复盘与隐私控制；不要编造未发生的录音内容。"
    )
    url = f"{base}/v1/chat/completions"
    payload = {
        "model": model,
        "messages": [
            {"role": "system", "content": system_prompt},
            {"role": "user", "content": user_text},
        ],
        "temperature": 0.6,
    }
    headers = {"Authorization": f"Bearer {key}", "Content-Type": "application/json"}
    async with httpx.AsyncClient(timeout=120.0) as client:
        r = await client.post(url, json=payload, headers=headers)
        r.raise_for_status()
        data = r.json()
        return data["choices"][0]["message"]["content"].strip()
