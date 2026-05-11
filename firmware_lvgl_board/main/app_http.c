#include "app_http.h"
#include "sdkconfig.h"
#include "cJSON.h"
#include "esp_http_client.h"
#include "esp_log.h"
#include <stdlib.h>
#include <string.h>

static const char *TAG = "app_http";

const char *app_http_base_url(void)
{
    return CONFIG_AIW_SERVER_BASE_URL;
}

/**
 * POST 固定长度正文：先写 hdr，再分块写 data，避免 malloc(body_len) 在堆紧张时 ESP_ERR_NO_MEM。
 */
static esp_err_t http_post_hdr_then_data(const char *url, const char *content_type,
                                         const uint8_t *hdr, size_t hdr_len,
                                         const uint8_t *data, size_t data_len,
                                         char *resp_out, size_t resp_out_len,
                                         int timeout_ms)
{
    const size_t total_len = hdr_len + data_len;
    esp_http_client_config_t cfg = {
        .url = url,
        .method = HTTP_METHOD_POST,
        .timeout_ms = timeout_ms,
    };
    esp_http_client_handle_t cl = esp_http_client_init(&cfg);
    if (!cl) {
        return ESP_FAIL;
    }
    esp_http_client_set_header(cl, "Content-Type", content_type);
    esp_http_client_set_header(cl, "X-Device-Name", CONFIG_AIW_DEVICE_NAME);
    esp_err_t err = esp_http_client_open(cl, (int)total_len);
    if (err != ESP_OK) {
        esp_http_client_cleanup(cl);
        return err;
    }
    if (hdr_len && hdr) {
        int w = esp_http_client_write(cl, (const char *)hdr, (int)hdr_len);
        if (w != (int)hdr_len) {
            ESP_LOGW(TAG, "hdr write %d != %u", w, (unsigned)hdr_len);
        }
    }
    for (size_t off = 0; off < data_len;) {
        size_t chunk = data_len - off;
        if (chunk > 8192) {
            chunk = 8192;
        }
        int w = esp_http_client_write(cl, (const char *)(data + off), (int)chunk);
        if (w <= 0) {
            ESP_LOGW(TAG, "body write failed at off=%u", (unsigned)off);
            esp_http_client_close(cl);
            esp_http_client_cleanup(cl);
            return ESP_FAIL;
        }
        off += (size_t)w;
    }
    esp_http_client_fetch_headers(cl);
    int st = esp_http_client_get_status_code(cl);
    if (resp_out && resp_out_len) {
        resp_out[0] = '\0';
        size_t got = 0;
        while (got + 1 < resp_out_len) {
            int r = esp_http_client_read(cl, resp_out + got, (int)(resp_out_len - 1 - got));
            if (r <= 0) {
                break;
            }
            got += (size_t)r;
            resp_out[got] = '\0';
        }
    } else {
        char drain[256];
        while (esp_http_client_read(cl, drain, sizeof(drain)) > 0) {
        }
    }
    esp_http_client_close(cl);
    esp_http_client_cleanup(cl);
    if (st < 200 || st >= 300) {
        ESP_LOGE(TAG, "HTTP status %d", st);
        return ESP_FAIL;
    }
    return ESP_OK;
}

static esp_err_t http_post_raw(const char *url, const char *content_type, const uint8_t *body, size_t body_len,
                               char *resp_out, size_t resp_out_len)
{
    esp_http_client_config_t cfg = {
        .url = url,
        .method = HTTP_METHOD_POST,
        .timeout_ms = 60000,
    };
    esp_http_client_handle_t cl = esp_http_client_init(&cfg);
    if (!cl) {
        return ESP_FAIL;
    }
    esp_http_client_set_header(cl, "Content-Type", content_type);
    esp_http_client_set_header(cl, "X-Device-Name", CONFIG_AIW_DEVICE_NAME);
    esp_err_t err = esp_http_client_open(cl, (int)body_len);
    if (err != ESP_OK) {
        esp_http_client_cleanup(cl);
        return err;
    }
    if (body_len && body) {
        int w = esp_http_client_write(cl, (const char *)body, (int)body_len);
        if (w != (int)body_len) {
            ESP_LOGW(TAG, "write %d != %u", w, (unsigned)body_len);
        }
    }
    esp_http_client_fetch_headers(cl);
    int st = esp_http_client_get_status_code(cl);
    if (resp_out && resp_out_len) {
        resp_out[0] = '\0';
        size_t got = 0;
        while (got + 1 < resp_out_len) {
            int r = esp_http_client_read(cl, resp_out + got, (int)(resp_out_len - 1 - got));
            if (r <= 0) {
                break;
            }
            got += (size_t)r;
            resp_out[got] = '\0';
        }
    } else {
        char drain[256];
        while (esp_http_client_read(cl, drain, sizeof(drain)) > 0) {
        }
    }
    esp_http_client_close(cl);
    esp_http_client_cleanup(cl);
    if (st < 200 || st >= 300) {
        ESP_LOGE(TAG, "HTTP status %d", st);
        return ESP_FAIL;
    }
    return ESP_OK;
}

esp_err_t app_http_post_json(const char *path, const char *json_body, char *out, size_t out_len)
{
    char url[256];
    snprintf(url, sizeof(url), "%s%s", CONFIG_AIW_SERVER_BASE_URL, path);
    return http_post_raw(url, "application/json; charset=utf-8", (const uint8_t *)json_body,
                           json_body ? strlen(json_body) : 0, out, out_len);
}

static void build_wav_header(uint8_t hdr[44], uint32_t pcm_len, uint32_t sample_rate_hz)
{
    uint32_t byte_rate = sample_rate_hz * 2;
    uint32_t subchunk2 = pcm_len;
    uint32_t chunk_size = 36 + subchunk2;
    memcpy(hdr + 0, "RIFF", 4);
    memcpy(hdr + 4, &chunk_size, 4);
    memcpy(hdr + 8, "WAVE", 4);
    memcpy(hdr + 12, "fmt ", 4);
    uint32_t sc1 = 16;
    memcpy(hdr + 16, &sc1, 4);
    uint16_t af = 1;
    memcpy(hdr + 20, &af, 2);
    uint16_t ch = 1;
    memcpy(hdr + 22, &ch, 2);
    memcpy(hdr + 24, &sample_rate_hz, 4);
    memcpy(hdr + 28, &byte_rate, 4);
    uint16_t ba = 2;
    memcpy(hdr + 32, &ba, 2);
    uint16_t bps = 16;
    memcpy(hdr + 34, &bps, 2);
    memcpy(hdr + 36, "data", 4);
    memcpy(hdr + 40, &subchunk2, 4);
}

esp_err_t app_http_upload_wav_pcm(const uint8_t *pcm, size_t pcm_len, uint32_t sample_rate_hz)
{
    if (!pcm || !pcm_len) {
        return ESP_ERR_INVALID_ARG;
    }
    uint8_t hdr[44];
    build_wav_header(hdr, (uint32_t)pcm_len, sample_rate_hz);

    char url[256];
    snprintf(url, sizeof(url), "%s/api/recordings/upload", CONFIG_AIW_SERVER_BASE_URL);

    esp_err_t err = http_post_hdr_then_data(url, "audio/wav", hdr, sizeof(hdr), pcm, pcm_len, NULL, 0, 60000);
    if (err == ESP_OK) {
        ESP_LOGI(TAG, "upload ok, %u bytes wav", (unsigned)(44 + pcm_len));
    }
    return err;
}

esp_err_t app_http_post_pcm_voice_chat(const uint8_t *pcm, size_t pcm_len, uint32_t sample_rate_hz,
                                       char *json_out, size_t json_out_len)
{
    if (!pcm || pcm_len < 64 || !json_out || json_out_len < 64) {
        return ESP_ERR_INVALID_ARG;
    }
    uint8_t hdr[44];
    build_wav_header(hdr, (uint32_t)pcm_len, sample_rate_hz);

    char url[256];
    snprintf(url, sizeof(url), "%s/api/chat/from_wav", CONFIG_AIW_SERVER_BASE_URL);
    /* STT + DeepSeek 可能较慢 */
    return http_post_hdr_then_data(url, "audio/wav", hdr, sizeof(hdr), pcm, pcm_len, json_out, json_out_len,
                                   120000);
}

esp_err_t app_http_fetch_tts_wav(const char *utf8_text, uint8_t **wav_out, size_t *wav_len_out)
{
    *wav_out = NULL;
    *wav_len_out = 0;
    if (!utf8_text || !utf8_text[0]) {
        return ESP_ERR_INVALID_ARG;
    }

    cJSON *root = cJSON_CreateObject();
    if (!root) {
        return ESP_ERR_NO_MEM;
    }
    cJSON_AddStringToObject(root, "text", utf8_text);
    char *body = cJSON_PrintUnformatted(root);
    cJSON_Delete(root);
    if (!body) {
        return ESP_ERR_NO_MEM;
    }

    char url[256];
    snprintf(url, sizeof(url), "%s/api/tts", CONFIG_AIW_SERVER_BASE_URL);

    esp_http_client_config_t cfg = {
        .url = url,
        .method = HTTP_METHOD_POST,
        .timeout_ms = 90000,
    };
    esp_http_client_handle_t cl = esp_http_client_init(&cfg);
    if (!cl) {
        free(body);
        return ESP_FAIL;
    }
    esp_http_client_set_header(cl, "Content-Type", "application/json; charset=utf-8");
    esp_http_client_set_header(cl, "X-Device-Name", CONFIG_AIW_DEVICE_NAME);

    size_t blen = strlen(body);
    esp_err_t er = esp_http_client_open(cl, (int)blen);
    if (er != ESP_OK) {
        free(body);
        esp_http_client_cleanup(cl);
        return er;
    }
    int w = esp_http_client_write(cl, body, (int)blen);
    free(body);
    if (w != (int)blen) {
        esp_http_client_close(cl);
        esp_http_client_cleanup(cl);
        return ESP_FAIL;
    }

    esp_http_client_fetch_headers(cl);
    int st = esp_http_client_get_status_code(cl);

    char drain[256];
    if (st != 200) {
        while (esp_http_client_read(cl, drain, sizeof(drain)) > 0) {
        }
        esp_http_client_close(cl);
        esp_http_client_cleanup(cl);
        ESP_LOGW(TAG, "tts HTTP %d", st);
        return ESP_FAIL;
    }

    const size_t k_max = 512 * 1024;
    size_t cap = 8192;
    uint8_t *buf = (uint8_t *)malloc(cap);
    if (!buf) {
        esp_http_client_close(cl);
        esp_http_client_cleanup(cl);
        return ESP_ERR_NO_MEM;
    }
    size_t total = 0;
    for (;;) {
        if (total >= k_max) {
            ESP_LOGW(TAG, "tts payload too large");
            free(buf);
            while (esp_http_client_read(cl, drain, sizeof(drain)) > 0) {
            }
            esp_http_client_close(cl);
            esp_http_client_cleanup(cl);
            return ESP_ERR_INVALID_SIZE;
        }
        size_t room = cap - total;
        if (room < 2048) {
            size_t grow = cap > 65536 ? 65536 : cap;
            size_t ncap = cap + grow;
            if (ncap > k_max) {
                ncap = k_max;
            }
            if (ncap <= cap) {
                free(buf);
                esp_http_client_close(cl);
                esp_http_client_cleanup(cl);
                return ESP_ERR_INVALID_SIZE;
            }
            uint8_t *nb = (uint8_t *)realloc(buf, ncap);
            if (!nb) {
                free(buf);
                esp_http_client_close(cl);
                esp_http_client_cleanup(cl);
                return ESP_ERR_NO_MEM;
            }
            buf = nb;
            cap = ncap;
            room = cap - total;
        }
        int rd = esp_http_client_read(cl, (char *)buf + total, (int)room);
        if (rd <= 0) {
            break;
        }
        total += (size_t)rd;
    }
    esp_http_client_close(cl);
    esp_http_client_cleanup(cl);

    if (total < 44) {
        free(buf);
        return ESP_FAIL;
    }
    *wav_out = buf;
    *wav_len_out = total;
    return ESP_OK;
}
