#include "app_http.h"
#include "sdkconfig.h"
#include "esp_http_client.h"
#include "esp_log.h"
#include <stdlib.h>
#include <string.h>

static const char *TAG = "app_http";

const char *app_http_base_url(void)
{
    return CONFIG_AIW_SERVER_BASE_URL;
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
        int r = esp_http_client_read(cl, resp_out, (int)resp_out_len - 1);
        if (r > 0) {
            resp_out[r] = '\0';
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
    size_t total = 44 + pcm_len;
    uint8_t *buf = malloc(total);
    if (!buf) {
        return ESP_ERR_NO_MEM;
    }
    build_wav_header(buf, (uint32_t)pcm_len, sample_rate_hz);
    memcpy(buf + 44, pcm, pcm_len);

    char url[256];
    snprintf(url, sizeof(url), "%s/api/recordings/upload", CONFIG_AIW_SERVER_BASE_URL);

    esp_err_t err = http_post_raw(url, "audio/wav", buf, total, NULL, 0);
    free(buf);
    if (err == ESP_OK) {
        ESP_LOGI(TAG, "upload ok, %u bytes wav", (unsigned)total);
    }
    return err;
}
