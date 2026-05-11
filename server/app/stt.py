"""Speech-to-text: 优先百度 ASR（配置 BAIDU_* 时），否则 faster-whisper（需 Hugging Face）。"""

import io
import os
import wave
from typing import Optional

import numpy as np

_model = None
_model_name: Optional[str] = None


def _use_baidu_asr() -> bool:
    return bool(os.getenv("BAIDU_API_KEY", "").strip() and os.getenv("BAIDU_SECRET_KEY", "").strip())


def _load_model():
    global _model, _model_name
    name = os.getenv("WHISPER_MODEL", "tiny").strip() or "tiny"
    if _model is not None and _model_name == name:
        return _model
    try:
        from faster_whisper import WhisperModel
    except ImportError as e:
        raise RuntimeError(
            "未安装 faster-whisper。请在 server 目录执行: pip install faster-whisper"
        ) from e

    device = os.getenv("WHISPER_DEVICE", "cpu")
    ctype = os.getenv("WHISPER_COMPUTE_TYPE", "int8")
    _model = WhisperModel(name, device=device, compute_type=ctype)
    _model_name = name
    return _model


def wav_bytes_to_float32_mono(data: bytes) -> tuple[np.ndarray, int]:
    bio = io.BytesIO(data)
    with wave.open(bio, "rb") as wf:
        nch = wf.getnchannels()
        sw = wf.getsampwidth()
        sr = wf.getframerate()
        nframes = wf.getnframes()
        raw = wf.readframes(nframes)
    if sw != 2:
        raise ValueError("only 16-bit WAV supported")
    pcm = np.frombuffer(raw, dtype=np.int16).astype(np.float32) / 32768.0
    if nch == 2:
        pcm = pcm.reshape(-1, 2).mean(axis=1)
    return pcm, int(sr)


def transcribe_wav_bytes(data: bytes, language: Optional[str] = None) -> str:
    """
    Returns stripped transcript (may be empty).
    language: 仅对 Whisper 路径有效；百度侧由 dev_pid 控制。
    """
    if len(data) < 2048:
        return ""

    if _use_baidu_asr():
        from . import baidu_asr

        return baidu_asr.transcribe_wav_bytes(data)

    pcm, sr = wav_bytes_to_float32_mono(data)
    if pcm.size < 800:
        return ""

    model = _load_model()
    if language is not None:
        lang = language.strip() or None
    else:
        lang = os.getenv("WHISPER_LANGUAGE", "zh").strip() or None

    if sr != 16000:
        new_len = max(1, int(len(pcm) * 16000 / sr))
        xi = np.linspace(0.0, float(len(pcm) - 1), new_len, dtype=np.float64)
        pcm = np.interp(xi, np.arange(len(pcm), dtype=np.float64), pcm.astype(np.float64)).astype(np.float32)

    segments, _info = model.transcribe(
        pcm,
        language=lang,
        task="transcribe",
        vad_filter=True,
        beam_size=5,
    )
    parts: list[str] = []
    for seg in segments:
        t = (seg.text or "").strip()
        if t:
            parts.append(t)
    return " ".join(parts).strip()
