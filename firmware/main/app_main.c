#include "nvs_flash.h"
#include "sdkconfig.h"
#ifndef CONFIG_AIW_MENUCONFIG_WIFI_FALLBACK
#define CONFIG_AIW_MENUCONFIG_WIFI_FALLBACK 0
#endif
#include "esp_log.h"
#include "iic.h"
#include "xl9555.h"
#include "app_cred.h"
#include "app_wifi.h"
#include "app_http.h"
#include "app_prov.h"
#include "app_audio_codec.h"
#include "app_console.h"

#include <string.h>

static const char *TAG = "main";

void app_main(void)
{
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK(ret);

    /* I2C0: XL9555；I2C1: ES8311 — 无屏、无 SPI/LCD/触摸 */
    ESP_ERROR_CHECK(myiic_init());
    ESP_ERROR_CHECK(myiic_init1());
    ESP_ERROR_CHECK(xl9555_init());

    char ssid[CRED_SSID_MAX];
    char pass[CRED_PASS_MAX];
    char url[CRED_URL_MAX];
    bool has_nvs = cred_load_all(ssid, sizeof ssid, pass, sizeof pass, url, sizeof url);
    if (!has_nvs && CONFIG_AIW_MENUCONFIG_WIFI_FALLBACK) {
        strncpy(ssid, CONFIG_AIW_WIFI_SSID, sizeof ssid - 1);
        ssid[sizeof ssid - 1] = '\0';
        strncpy(pass, CONFIG_AIW_WIFI_PASSWORD, sizeof pass - 1);
        pass[sizeof pass - 1] = '\0';
    } else if (!has_nvs) {
        ssid[0] = '\0';
        pass[0] = '\0';
        ESP_LOGI(TAG, "no NVS WiFi and menuconfig fallback disabled -> SoftAP provisioning");
    }
    app_http_set_base_url(url);

    ESP_ERROR_CHECK(app_wifi_system_init());

    bool sta_ok = false;
    if (ssid[0] != '\0') {
        ESP_LOGI(TAG, "try STA (NVS=%s)", has_nvs ? "yes" : "menuconfig fallback");
        ESP_ERROR_CHECK(app_wifi_sta_set_and_start(ssid, pass));
        if (app_wifi_wait_connected(60000) == ESP_OK) {
            sta_ok = true;
        } else {
            ESP_LOGW(TAG, "STA connect failed or timeout -> provisioning SoftAP");
        }
    } else {
        ESP_LOGW(TAG, "empty SSID -> provisioning SoftAP");
    }

    if (!sta_ok) {
        ESP_ERROR_CHECK(app_wifi_stop());
        ESP_ERROR_CHECK(app_wifi_ap_start_provisioning());
        app_prov_http_server_start();
        /* 配网成功会 esp_restart；否则阻塞在 HTTP 服务 */
    }

    ESP_LOGI(TAG, "Wi-Fi OK, IP: %s", app_wifi_ip_str());

    app_audio_init();
    app_console_start();

    while (1) {
        vTaskDelay(pdMS_TO_TICKS(10000));
    }
}
