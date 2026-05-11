"""百度语音识别（短语音 REST），替代本机 Whisper / Hugging Face 下载。

密钥放在环境变量 BAIDU_API_KEY / BAIDU_SECRET_KEY（见 server/.env），勿提交仓库。
文档：https://ai.baidu.com/ai-doc/SPEECH/Vk38lxily
"""

from __future__ import annotations

import base64
import os

import httpx

from .baidu_auth import get_access_token


def transcribe_wav_bytes(wav_bytes: bytes) -> str:
    """
    传入完整 WAV 文件字节（含 44 字节头），16kHz 单声道 16bit 与手表一致。
    返回识别文本（可能为空）。
    """
    if len(wav_bytes) < 1000:
        return ""

    token = get_access_token()
    speech_b64 = base64.b64encode(wav_bytes).decode("ascii")

    dev_pid = os.getenv("BAIDU_ASR_DEV_PID", "1537").strip() or "1537"
    try:
        dev_pid_int = int(dev_pid)
    except ValueError:
        dev_pid_int = 1537

    payload = {
        "format": "wav",
        "rate": 16000,
        "channel": 1,
        "cuid": os.getenv("BAIDU_ASR_CUID", "ai-watch-pc").strip() or "ai-watch-pc",
        "token": token,
        "speech": speech_b64,
        "len": len(wav_bytes),
        "dev_pid": dev_pid_int,
    }

    with httpx.Client(timeout=60.0) as client:
        r = client.post(
            "https://vop.baidu.com/server_api",
            json=payload,
            headers={"Content-Type": "application/json; charset=utf-8"},
        )
        r.raise_for_status()
        data = r.json()

    err_no = data.get("err_no")
    if err_no != 0:
        msg = data.get("err_msg", "")
        raise RuntimeError(f"Baidu ASR err_no={err_no} err_msg={msg}")

    result = data.get("result")
    if isinstance(result, list) and result:
        return "".join(result).strip()
    return ""
