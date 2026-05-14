/**
 * 方案 A：基于幻尔 14_lvgl_conversion_of_number 的 main，
 * 在 lvgl_demo() 前加入 WiFi + 板载 ES8311（与 Ai Watch 逻辑一致）。
 */
#include "nvs_flash.h"
#include "sdkconfig.h"
#include "esp_log.h"
#include "esp_heap_caps.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "led.h"
#include "iic.h"
#include "xl9555.h"
#include "lvgl_demo.h"
#include "spi.h"
#include "app_cred.h"
#include "app_wifi.h"
#include "app_http.h"
#include "app_prov.h"
#include "app_audio_codec.h"
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

    led_init();
    /* 与教程一致：I2C1 先起；XL9555 内部会按需 myiic_init() I2C0 */
    ESP_ERROR_CHECK(myiic_init1());
    ESP_ERROR_CHECK(xl9555_init());
    spi2_init();

    char ssid[CRED_SSID_MAX];
    char pass[CRED_PASS_MAX];
    char url[CRED_URL_MAX];
    bool has_nvs = cred_load_all(ssid, sizeof ssid, pass, sizeof pass, url, sizeof url);
    if (!has_nvs) {
        strncpy(ssid, CONFIG_AIW_WIFI_SSID, sizeof ssid - 1);
        ssid[sizeof ssid - 1] = '\0';
        strncpy(pass, CONFIG_AIW_WIFI_PASSWORD, sizeof pass - 1);
        pass[sizeof pass - 1] = '\0';
    }
    app_http_set_base_url(url);

    ESP_ERROR_CHECK(app_wifi_system_init());

    bool sta_ok = false;
    if (ssid[0] != '\0') {
        ESP_LOGI(TAG, "Wi-Fi: try STA (NVS saved=%s)", has_nvs ? "yes" : "no, menuconfig fallback)");
        ESP_ERROR_CHECK(app_wifi_sta_set_and_start(ssid, pass));
        if (app_wifi_wait_connected(60000) == ESP_OK) {
            sta_ok = true;
        } else {
            ESP_LOGW(TAG, "STA failed or timeout -> enter provisioning SoftAP");
        }
    } else {
        ESP_LOGW(TAG, "empty SSID -> provisioning SoftAP");
    }

    if (!sta_ok) {
        ESP_ERROR_CHECK(app_wifi_stop());
        ESP_ERROR_CHECK(app_wifi_ap_start_provisioning());
        /* 阻塞：手机连热点后浏览器 http://192.168.4.1 配网，保存后重启 */
        app_prov_http_server_start();
    }

    ESP_LOGI(TAG, "Wi-Fi OK, IP: %s", app_wifi_ip_str());

    /* 先 LVGL 显示 DMA 缓冲，再音频 PCM，避免内部堆耗尽 → lv_disp buf NULL → 重启 */
    lvgl_engine_init();
    app_audio_init();
    vTaskDelay(pdMS_TO_TICKS(300));
    app_audio_play_boot_chime();

    lvgl_start_demo_tasks();

#if CONFIG_SPIRAM
    {
        size_t spiram_free = heap_caps_get_free_size(MALLOC_CAP_SPIRAM);
        size_t internal_free = heap_caps_get_free_size(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
        ESP_LOGI(TAG, "Heap: SPIRAM free=%u KB, internal free=%u KB — PSRAM driver enabled",
                 (unsigned)(spiram_free / 1024u), (unsigned)(internal_free / 1024u));
    }
#else
    ESP_LOGW(TAG, "CONFIG_SPIRAM off in sdkconfig — 8MB PSRAM not used; enable in menuconfig/sdkconfig.defaults");
#endif
}
