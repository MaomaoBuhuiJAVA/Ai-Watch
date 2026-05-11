#pragma once

#define BOARD_AUDIO_SAMPLE_RATE 16000
/** 录音 PCM 上限；1MB 在无 PSRAM 板子上 WiFi 起来后常分配失败，导致整段音频不初始化 */
#define BOARD_RECORD_MAX_BYTES (512 * 1024)
#define BOARD_RECORD_MIN_BYTES (64 * 1024)
