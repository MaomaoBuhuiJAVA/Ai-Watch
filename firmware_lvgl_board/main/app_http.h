#pragma once

#include "esp_err.h"
#include <stddef.h>
#include <stdint.h>

const char *app_http_base_url(void);

/** POST application/json，返回 200 时把响应体写入 out（需预先分配） */
esp_err_t app_http_post_json(const char *path, const char *json_body, char *out, size_t out_len);

/** 上传 16bit LE mono PCM 为 WAV（服务端存盘） */
esp_err_t app_http_upload_wav_pcm(const uint8_t *pcm, size_t pcm_len, uint32_t sample_rate_hz);

/**
 * 将 16bit LE mono PCM 封装为 WAV 并 POST /api/chat/from_wav。
 * 成功时 json_out 为 UTF-8 JSON：{"transcript":"...","reply":"..."}
 */
esp_err_t app_http_post_pcm_voice_chat(const uint8_t *pcm, size_t pcm_len, uint32_t sample_rate_hz,
                                       char *json_out, size_t json_out_len);

/**
 * POST /api/tts，body 为 {"text":"..."}；成功时分配 *wav_out（调用方 free），长度 *wav_len_out。
 */
esp_err_t app_http_fetch_tts_wav(const char *utf8_text, uint8_t **wav_out, size_t *wav_len_out);
