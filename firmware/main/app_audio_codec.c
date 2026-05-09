#include "app_audio_codec.h"
#include "board_config.h"
#include "app_http.h"
#include "iic.h"
#include "xl9555.h"
#include "nvs.h"
#include "nvs_flash.h"
#include "driver/i2s_std.h"
#include "esp_codec_dev.h"
#include "esp_codec_dev_defaults.h"
#include "esp_codec_dev_types.h"
#include "esp_heap_caps.h"
#include "esp_check.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <stdlib.h>
#include <string.h>

static const char *TAG = "audio_es8311";

/* 与官方 01_audio_es8311 例程一致 */
#define I2C_PORT_CODEC    I2C_NUM_1
#define ES8311_I2C_ADDR   0x30
#define I2S_MCK_IO        GPIO_NUM_45
#define I2S_BCK_IO        GPIO_NUM_39
#define I2S_WS_IO         GPIO_NUM_41
#define I2S_DO_IO         GPIO_NUM_42
#define I2S_DI_IO         GPIO_NUM_40

static esp_codec_dev_handle_t s_codec = NULL;
static i2s_chan_handle_t s_tx = NULL;
static i2s_chan_handle_t s_rx = NULL;
static const audio_codec_data_if_t *s_data_if = NULL;
static const audio_codec_ctrl_if_t *s_ctrl_if = NULL;
static const audio_codec_gpio_if_t *s_gpio_if = NULL;
static const audio_codec_if_t *s_codec_if = NULL;

static uint8_t *s_pcm = NULL;
static size_t s_pcm_len = 0;
static bool s_recording = false;
static TaskHandle_t s_cap_task = NULL;

static esp_err_t init_i2s(void)
{
    i2s_chan_config_t chan_cfg = {
        .id = I2S_NUM_0,
        .role = I2S_ROLE_MASTER,
        .dma_desc_num = 6,
        .dma_frame_num = 240,
        .auto_clear_after_cb = true,
        .auto_clear_before_cb = false,
        .intr_priority = 0,
    };
    ESP_RETURN_ON_ERROR(i2s_new_channel(&chan_cfg, &s_tx, &s_rx), TAG, "i2s_new_channel");
    i2s_std_config_t std_cfg = {
        .clk_cfg = {
            .sample_rate_hz = BOARD_AUDIO_SAMPLE_RATE,
            .clk_src = I2S_CLK_SRC_DEFAULT,
            .mclk_multiple = I2S_MCLK_MULTIPLE_256,
        },
        .slot_cfg = {
            .data_bit_width = I2S_DATA_BIT_WIDTH_16BIT,
            .slot_bit_width = I2S_SLOT_BIT_WIDTH_AUTO,
            .slot_mode = I2S_SLOT_MODE_STEREO,
            .slot_mask = I2S_STD_SLOT_BOTH,
            .ws_width = I2S_DATA_BIT_WIDTH_16BIT,
            .ws_pol = false,
            .bit_shift = true,
        },
        .gpio_cfg = {
            .mclk = I2S_MCK_IO,
            .bclk = I2S_BCK_IO,
            .ws = I2S_WS_IO,
            .dout = I2S_DO_IO,
            .din = I2S_DI_IO,
            .invert_flags = {.mclk_inv = false, .bclk_inv = false, .ws_inv = false},
        },
    };
    ESP_RETURN_ON_ERROR(i2s_channel_init_std_mode(s_tx, &std_cfg), TAG, "tx");
    ESP_RETURN_ON_ERROR(i2s_channel_init_std_mode(s_rx, &std_cfg), TAG, "rx");
    return ESP_OK;
}

static esp_err_t init_es8311(void)
{
    if (bus_handle1 == NULL) {
        ESP_LOGE(TAG, "I2C1 not initialized; call myiic_init1() first");
        return ESP_ERR_INVALID_STATE;
    }

    xl9555_pin_write(MUTE, 1);
    vTaskDelay(pdMS_TO_TICKS(80));

    ESP_RETURN_ON_ERROR(init_i2s(), TAG, "i2s init");

    audio_codec_i2s_cfg_t i2s_cfg = {
        .port = I2S_NUM_0,
        .rx_handle = s_rx,
        .tx_handle = s_tx,
    };
    s_data_if = audio_codec_new_i2s_data(&i2s_cfg);
    if (!s_data_if) {
        return ESP_FAIL;
    }

    audio_codec_i2c_cfg_t i2c_cfg = {
        .port = I2C_PORT_CODEC,
        .addr = ES8311_I2C_ADDR,
        .bus_handle = bus_handle1,
    };
    s_ctrl_if = audio_codec_new_i2c_ctrl(&i2c_cfg);
    if (!s_ctrl_if) {
        return ESP_FAIL;
    }

    s_gpio_if = audio_codec_new_gpio();
    if (!s_gpio_if) {
        return ESP_FAIL;
    }

    es8311_codec_cfg_t es8311_cfg = {};
    es8311_cfg.ctrl_if = s_ctrl_if;
    es8311_cfg.gpio_if = s_gpio_if;
    es8311_cfg.codec_mode = ESP_CODEC_DEV_WORK_MODE_BOTH;
    es8311_cfg.pa_pin = GPIO_NUM_NC;
    es8311_cfg.use_mclk = true;
    es8311_cfg.hw_gain.pa_voltage = 5.0f;
    es8311_cfg.hw_gain.codec_dac_voltage = 3.3f;
    es8311_cfg.pa_reverted = false;

    s_codec_if = es8311_codec_new(&es8311_cfg);
    if (!s_codec_if) {
        return ESP_FAIL;
    }

    esp_codec_dev_cfg_t dev_cfg = {
        .dev_type = ESP_CODEC_DEV_TYPE_IN_OUT,
        .codec_if = s_codec_if,
        .data_if = s_data_if,
    };
    s_codec = esp_codec_dev_new(&dev_cfg);
    if (!s_codec) {
        return ESP_FAIL;
    }

    esp_codec_dev_sample_info_t fs = {
        .bits_per_sample = 16,
        .channel = 1,
        .channel_mask = ESP_CODEC_DEV_MAKE_CHANNEL_MASK(0),
        .sample_rate = BOARD_AUDIO_SAMPLE_RATE,
        .mclk_multiple = 0,
    };

    esp_err_t oerr = esp_codec_dev_open(s_codec, &fs);
    if (oerr != ESP_OK) {
        ESP_LOGE(TAG, "codec open failed: %s", esp_err_to_name(oerr));
        return oerr;
    }
    ESP_ERROR_CHECK(esp_codec_dev_set_in_gain(s_codec, 25.0f));
    ESP_ERROR_CHECK(esp_codec_dev_set_out_vol(s_codec, 70));

    ESP_LOGI(TAG, "ES8311 ok (板载麦/喇叭)");
    return ESP_OK;
}

static esp_err_t flush_upload_if_allowed(void)
{
    bool privacy = false;
    nvs_handle_t h;
    if (nvs_open("aiw", NVS_READONLY, &h) == ESP_OK) {
        uint8_t v = 0;
        if (nvs_get_u8(h, "privacy", &v) == ESP_OK) {
            privacy = v != 0;
        }
        nvs_close(h);
    }
    if (privacy) {
        ESP_LOGI(TAG, "privacy on, skip upload");
        return ESP_OK;
    }
    if (s_pcm_len < sizeof(int16_t) * 50) {
        ESP_LOGW(TAG, "too short, skip upload");
        return ESP_ERR_INVALID_SIZE;
    }
    return app_http_upload_wav_pcm(s_pcm, s_pcm_len, BOARD_AUDIO_SAMPLE_RATE);
}

static void capture_task(void *arg)
{
    (void)arg;
    uint8_t chunk[512];
    while (1) {
        if (!s_recording || !s_codec) {
            vTaskDelay(pdMS_TO_TICKS(20));
            continue;
        }
        int want = sizeof(chunk);
        int ret = esp_codec_dev_read(s_codec, chunk, want);
        if (ret != ESP_CODEC_DEV_OK) {
            if (ret != ESP_CODEC_DEV_WRONG_STATE) {
                ESP_LOGD(TAG, "read ret=%d", ret);
            }
            vTaskDelay(pdMS_TO_TICKS(5));
            continue;
        }
        if (s_pcm_len + (size_t)want > BOARD_RECORD_MAX_BYTES) {
            ESP_LOGW(TAG, "buffer full");
            s_recording = false;
            flush_upload_if_allowed();
            continue;
        }
        memcpy(s_pcm + s_pcm_len, chunk, (size_t)want);
        s_pcm_len += (size_t)want;
    }
}

void app_audio_init(void)
{
    if (s_codec) {
        return;
    }

    s_pcm = (uint8_t *)heap_caps_malloc(BOARD_RECORD_MAX_BYTES, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (!s_pcm) {
        s_pcm = (uint8_t *)malloc(BOARD_RECORD_MAX_BYTES);
    }
    if (!s_pcm) {
        ESP_LOGE(TAG, "pcm buffer alloc failed");
        return;
    }

    if (init_es8311() != ESP_OK) {
        ESP_LOGE(TAG, "ES8311 init failed");
        return;
    }

    xTaskCreatePinnedToCore(capture_task, "audio_cap", 6144, NULL, 5, &s_cap_task, 1);
}

bool app_audio_recording(void)
{
    return s_recording;
}

void app_audio_record_set(bool on)
{
    if (on && !s_recording) {
        s_pcm_len = 0;
        s_recording = true;
        ESP_LOGI(TAG, "record start");
    } else if (!on && s_recording) {
        s_recording = false;
        ESP_LOGI(TAG, "record stop, bytes=%u", (unsigned)s_pcm_len);
        flush_upload_if_allowed();
    }
}

void app_audio_record_toggle(void)
{
    app_audio_record_set(!s_recording);
}

size_t app_audio_pcm_bytes(void)
{
    return s_pcm_len;
}
