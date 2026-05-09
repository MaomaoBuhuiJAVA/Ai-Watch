#pragma once

/** 板载 ES8311 录音/播放采样率 */
#define BOARD_AUDIO_SAMPLE_RATE 16000

/** 单次录音最大字节数（受 PSRAM 与上传限制，可调） */
#define BOARD_RECORD_MAX_BYTES (1024 * 1024)
