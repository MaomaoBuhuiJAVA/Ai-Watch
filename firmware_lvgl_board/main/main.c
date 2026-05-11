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
#include "app_wifi.h"
#include "app_audio_codec.h"

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

    ESP_LOGI(TAG, "Wi-Fi STA (SSID/password: menuconfig → Ai Watch, default iPhone / 12345678)");
    ESP_ERROR_CHECK(app_wifi_init_sta());
    if (app_wifi_wait_connected(120000) != ESP_OK) {
        ESP_LOGE(TAG, "Wi-Fi failed — check hotspot name/password and 2.4 GHz");
    } else {
        ESP_LOGI(TAG, "Wi-Fi OK, IP: %s", app_wifi_ip_str());
    }

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
