#include "app_cred.h"
#include "sdkconfig.h"
#include "nvs.h"
#include "nvs_flash.h"
#include "esp_log.h"
#include <stdio.h>
#include <string.h>

static const char *TAG = "cred";
static const char *NS = "aiw";

static void safe_copy(char *dst, size_t sz, const char *src)
{
    if (!dst || sz == 0) {
        return;
    }
    if (!src) {
        src = "";
    }
    size_t n = strnlen(src, sz - 1);
    memcpy(dst, src, n);
    dst[n] = '\0';
}

bool cred_load_all(char *ssid, size_t ssid_sz, char *pass, size_t pass_sz, char *url, size_t url_sz)
{
    if (!ssid || ssid_sz < 2 || !pass || pass_sz < 2 || !url || url_sz < 8) {
        return false;
    }
    ssid[0] = '\0';
    pass[0] = '\0';
    safe_copy(url, url_sz, CONFIG_AIW_SERVER_BASE_URL);

    nvs_handle_t h;
    if (nvs_open(NS, NVS_READONLY, &h) != ESP_OK) {
        return false;
    }

    size_t l = ssid_sz;
    if (nvs_get_str(h, "wifi_ssid", ssid, &l) != ESP_OK) {
        ssid[0] = '\0';
    }
    l = pass_sz;
    if (nvs_get_str(h, "wifi_pass", pass, &l) != ESP_OK) {
        pass[0] = '\0';
    }
    l = url_sz;
    if (nvs_get_str(h, "srv_url", url, &l) != ESP_OK) {
        safe_copy(url, url_sz, CONFIG_AIW_SERVER_BASE_URL);
    }
    nvs_close(h);

    return ssid[0] != '\0';
}

esp_err_t cred_save_wifi_and_url(const char *ssid, const char *pass, const char *url)
{
    if (!ssid || !ssid[0]) {
        return ESP_ERR_INVALID_ARG;
    }
    nvs_handle_t h;
    esp_err_t err = nvs_open(NS, NVS_READWRITE, &h);
    if (err != ESP_OK) {
        return err;
    }
    err = nvs_set_str(h, "wifi_ssid", ssid);
    if (err == ESP_OK && pass) {
        err = nvs_set_str(h, "wifi_pass", pass);
    }
    if (err == ESP_OK && url && url[0]) {
        err = nvs_set_str(h, "srv_url", url);
    }
    if (err == ESP_OK) {
        err = nvs_commit(h);
    }
    nvs_close(h);
    if (err == ESP_OK) {
        ESP_LOGI(TAG, "saved WiFi + server URL to NVS");
    } else {
        ESP_LOGE(TAG, "nvs save failed: %s", esp_err_to_name(err));
    }
    return err;
}
