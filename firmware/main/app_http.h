#pragma once

#include "esp_err.h"
#include <stddef.h>
#include <stdint.h>

const char *app_http_base_url(void);

/** POST application/json，返回 200 时把响应体写入 out（需预先分配） */
esp_err_t app_http_post_json(const char *path, const char *json_body, char *out, size_t out_len);

/** 上传 16bit LE mono PCM 为 WAV（服务端存盘） */
esp_err_t app_http_upload_wav_pcm(const uint8_t *pcm, size_t pcm_len, uint32_t sample_rate_hz);
