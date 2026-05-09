#include "nvs_flash.h"
#include "esp_log.h"
#include "iic.h"
#include "xl9555.h"
#include "spi.h"
#include "board_lcd.h"
#include "touch.h"
#include "app_wifi.h"
#include "app_audio_codec.h"
#include "app_ui_lvgl.h"

static const char *TAG = "main";

void app_main(void)
{
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK(ret);

    ESP_ERROR_CHECK(myiic_init());
    ESP_ERROR_CHECK(myiic_init1());
    ESP_ERROR_CHECK(xl9555_init());
    spi2_init();
    lcd_init();
    ESP_ERROR_CHECK(tp_dev.init());

    lcd_clear(0xFFE0);
    ESP_LOGI(TAG, "connecting WiFi…");
    ESP_ERROR_CHECK(app_wifi_init_sta());
    if (app_wifi_wait_connected(45000) != ESP_OK) {
        ESP_LOGE(TAG, "WiFi failed, UI will still start");
    }

    app_audio_init();
    app_ui_lvgl_start();

    while (1) {
        vTaskDelay(pdMS_TO_TICKS(1000));
    }
}
