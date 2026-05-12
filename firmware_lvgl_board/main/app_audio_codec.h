#pragma once

#include "esp_err.h"
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

void app_audio_init(void);
/** ES8311 已打开且采集任务已创建；为 false 时按录音不会增长字节 */
bool app_audio_is_ready(void);
bool app_audio_recording(void);
void app_audio_record_set(bool on);
void app_audio_record_toggle(void);

size_t app_audio_pcm_bytes(void);

/** 指向内部录音缓冲区（仅当未开始新一轮录音时有效；读取请尽快 memcpy） */
const uint8_t *app_audio_pcm_data(void);

/**
 * 从缓冲区头部拷贝最多 want 字节 PCM 并丢弃（用于流式上传）。
 * got_out 返回实际拷贝字节数（偶数）。
 */
void app_audio_pcm_copy_and_consume(uint8_t *dst, size_t want, size_t *got_out);

/** 清空录音 PCM 缓冲（流式结束后调用） */
void app_audio_pcm_buffer_reset(void);

/** 将当前 PCM 缓冲区封装为 WAV 并上传到 /api/recordings/upload（需已停止录音） */
esp_err_t app_audio_flush_upload_recording(void);

/** 开机提示音（双音），在 app_audio_init 之后由 main 调用 */
void app_audio_play_boot_chime(void);

/** 播放 WAV（PCM16 LE 单声道；采样率宜为 BOARD_AUDIO_SAMPLE_RATE） */
void app_audio_play_wav(const uint8_t *wav, size_t wav_len);
