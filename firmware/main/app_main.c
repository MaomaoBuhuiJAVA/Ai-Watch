#include "nvs_flash.h"
#include "esp_log.h"
#include "iic.h"
#include "xl9555.h"
#include "app_wifi.h"
#include "app_audio_codec.h"
#include "app_console.h"

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

    ESP_LOGI(TAG, "Wi-Fi STA (SSID/password: menuconfig → Ai Watch)");
    ESP_ERROR_CHECK(app_wifi_init_sta());
    if (app_wifi_wait_connected(120000) != ESP_OK) {
        ESP_LOGE(TAG, "Wi-Fi failed — check SSID/password and 2.4 GHz hotspot");
    } else {
        ESP_LOGI(TAG, "Wi-Fi OK, IP: %s", app_wifi_ip_str());
    }

    app_audio_init();
    app_console_start();

    while (1) {
        vTaskDelay(pdMS_TO_TICKS(10000));
    }
}
