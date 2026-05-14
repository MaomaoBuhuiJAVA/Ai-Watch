#include "app_prov.h"
#include "app_cred.h"
#include "sdkconfig.h"
#include "esp_http_server.h"
#include "esp_log.h"
#include "esp_system.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <stdio.h>
#include <string.h>

static const char *TAG = "prov";

static void form_get_param(const char *body, const char *key, char *out, size_t outlen)
{
    out[0] = '\0';
    if (!body || !key || !out || outlen < 2) {
        return;
    }
    char prefix[48];
    snprintf(prefix, sizeof(prefix), "%s=", key);
    const char *s = strstr(body, prefix);
    if (!s) {
        return;
    }
    s += strlen(prefix);
    const char *amp = strchr(s, '&');
    size_t n = amp ? (size_t)(amp - s) : strlen(s);
    if (n >= outlen) {
        n = outlen - 1;
    }
    memcpy(out, s, n);
    out[n] = '\0';
    for (char *p = out; *p; p++) {
        if (*p == '+') {
            *p = ' ';
        }
    }
}

static esp_err_t h_get(httpd_req_t *req)
{
    char buf[1536];
    int n = snprintf(
        buf,
        sizeof(buf),
        "<!DOCTYPE html><html><head><meta charset=\"utf-8\"/><meta name=\"viewport\" content=\"width=device-width,"
        " initial-scale=1\"/><title>WiFi 配网</title></head><body style=\"font-family:sans-serif;padding:16px;"
        "background:#111;color:#eee;\">"
        "<h2>智能手表 配网</h2>"
        "<p>手机已连本开发板热点。填写 <b>2.4GHz</b> WiFi 与后端地址后保存，设备将重启并自动联网。</p>"
        "<form method=\"POST\" action=\"/save\">"
        "<p>WiFi 名称<br/><input name=\"wifi_ssid\" style=\"width:100%%;padding:8px;\" required/></p>"
        "<p>WiFi 密码<br/><input name=\"wifi_pass\" type=\"password\" style=\"width:100%%;padding:8px;\"/></p>"
        "<p>服务器地址（无尾斜杠）<br/><input name=\"srv_url\" style=\"width:100%%;padding:8px;\" value=\"%s\"/></p>"
        "<p><button type=\"submit\" style=\"padding:12px 24px;font-size:16px;\">保存并重启</button></p>"
        "</form></body></html>",
        CONFIG_AIW_SERVER_BASE_URL);
    if (n < 0 || n >= (int)sizeof(buf)) {
        return httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "overflow");
    }
    httpd_resp_set_type(req, "text/html; charset=utf-8");
    return httpd_resp_send(req, buf, n);
}

static esp_err_t h_save(httpd_req_t *req)
{
    if (req->content_len <= 0 || req->content_len > 640) {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "bad length");
        return ESP_FAIL;
    }
    char body[641];
    int to_read = (int)req->content_len;
    if (to_read >= (int)sizeof(body)) {
        to_read = (int)sizeof(body) - 1;
    }
    int r = httpd_req_recv(req, body, to_read);
    if (r <= 0) {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "no body");
        return ESP_FAIL;
    }
    body[r] = '\0';

    char ssid[CRED_SSID_MAX];
    char pass[CRED_PASS_MAX];
    char url[CRED_URL_MAX];
    form_get_param(body, "wifi_ssid", ssid, sizeof ssid);
    form_get_param(body, "wifi_pass", pass, sizeof pass);
    form_get_param(body, "srv_url", url, sizeof url);

    if (ssid[0] == '\0') {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "wifi_ssid required");
        return ESP_FAIL;
    }
    if (url[0] == '\0') {
        strncpy(url, CONFIG_AIW_SERVER_BASE_URL, sizeof url - 1);
        url[sizeof url - 1] = '\0';
    }

    esp_err_t err = cred_save_wifi_and_url(ssid, pass, url);
    if (err != ESP_OK) {
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "nvs");
        return ESP_FAIL;
    }

    httpd_resp_set_type(req, "text/html; charset=utf-8");
    const char *msg = "<html><body><p>已保存，设备重启中…</p></body></html>";
    httpd_resp_send(req, msg, HTTPD_RESP_USE_STRLEN);
    vTaskDelay(pdMS_TO_TICKS(500));
    esp_restart();
    return ESP_OK;
}

void app_prov_http_server_start(void)
{
    httpd_config_t cfg = HTTPD_DEFAULT_CONFIG();
    cfg.server_port = 80;
    cfg.stack_size = 10240;
    cfg.lru_purge_enable = true;

    httpd_handle_t server = NULL;
    esp_err_t err = httpd_start(&server, &cfg);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "httpd_start failed: %s", esp_err_to_name(err));
        return;
    }

    httpd_uri_t u_root = {.uri = "/", .method = HTTP_GET, .handler = h_get, .user_ctx = NULL};
    httpd_uri_t u_save = {.uri = "/save", .method = HTTP_POST, .handler = h_save, .user_ctx = NULL};
    httpd_register_uri_handler(server, &u_root);
    httpd_register_uri_handler(server, &u_save);

    ESP_LOGI(TAG, "HTTP 配网已启动，手机浏览器打开 http://192.168.4.1");

    while (1) {
        vTaskDelay(pdMS_TO_TICKS(10000));
    }
}
