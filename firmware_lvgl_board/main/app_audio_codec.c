#include "app_audio_codec.h"
#include "sdkconfig.h"
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
#include <math.h>
#include <stdlib.h>
#include <string.h>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

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
static size_t s_pcm_cap = 0;
static volatile size_t s_pcm_len = 0;
static volatile bool s_recording = false;
static TaskHandle_t s_cap_task = NULL;
static uint32_t s_cap_read_fail;

/* I2S dma_frame_num=240、16bit 立体声槽 → 单帧 4B；读长取 240 帧对齐，避免 read 失败 */
#define AUDIO_CAP_CHUNK_BYTES (240 * 4)

/** XL9555 上 MUTE/功放控制：有效电平见 menuconfig CONFIG_AIW_XL9555_SPK_HIGH_ENABLES_AMP */
static void aiw_speaker_amp_set(bool on)
{
#if CONFIG_AIW_XL9555_SPK_HIGH_ENABLES_AMP
    xl9555_pin_write(MUTE, on ? 1 : 0);
#else
    xl9555_pin_write(MUTE, on ? 0 : 1);
#endif
}

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

    /* 先关功放再上电 I2S，减轻上电冲击（“关”的电平随 Kconfig 反相） */
    aiw_speaker_amp_set(false);
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

    /* XF2718T09 模拟麦 → ES8311 MIC_P / MIC_N（MIC_N 经 0Ω 接地为伪差分）；勿开 PDM */
    es8311_codec_cfg_t es8311_cfg = {};
    es8311_cfg.ctrl_if = s_ctrl_if;
    es8311_cfg.gpio_if = s_gpio_if;
    es8311_cfg.codec_mode = ESP_CODEC_DEV_WORK_MODE_BOTH;
    es8311_cfg.digital_mic = false;
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

    int open_rc = esp_codec_dev_open(s_codec, &fs);
    if (open_rc != ESP_CODEC_DEV_OK) {
        ESP_LOGE(TAG, "codec open failed rc=%d (I2S/ES8311)", open_rc);
        esp_codec_dev_delete(s_codec);
        s_codec = NULL;
        return ESP_FAIL;
    }
    /* 增益失败勿 ESP_ERROR_CHECK（会 abort 重启）；仅记录 */
    int ir = esp_codec_dev_set_in_gain(s_codec, 32.0f);
    if (ir != ESP_CODEC_DEV_OK) {
        ESP_LOGW(TAG, "set_in_gain ret=%d", ir);
    }
    ir = esp_codec_dev_set_out_vol(s_codec, 82);
    if (ir != ESP_CODEC_DEV_OK) {
        ESP_LOGW(TAG, "set_out_vol ret=%d", ir);
    }

    aiw_speaker_amp_set(true);
    ESP_LOGI(TAG, "ES8311 ok; XL9555 amp=%s", CONFIG_AIW_XL9555_SPK_HIGH_ENABLES_AMP ? "HIGH=on" : "LOW=on");

    {
        int16_t smoke[AUDIO_CAP_CHUNK_BYTES / 2];
        int rr = esp_codec_dev_read(s_codec, smoke, sizeof(smoke));
        ESP_LOGI(TAG, "mic path smoke read ret=%d (OK=%d)", rr, ESP_CODEC_DEV_OK);
        if (rr != ESP_CODEC_DEV_OK) {
            ESP_LOGE(TAG, "mic read failed — check ES8311/I2S; ret=%d", rr);
        }
    }
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
        return ESP_ERR_NOT_SUPPORTED;
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
    uint8_t chunk[AUDIO_CAP_CHUNK_BYTES];
    while (1) {
        if (!s_recording || !s_codec) {
            vTaskDelay(pdMS_TO_TICKS(20));
            continue;
        }
        const int want = sizeof(chunk);
        int ret = esp_codec_dev_read(s_codec, chunk, want);
        if (ret != ESP_CODEC_DEV_OK) {
            s_cap_read_fail++;
            if (s_cap_read_fail == 1 || s_cap_read_fail % 80 == 0) {
                ESP_LOGW(TAG, "codec read ret=%d (fail #%u while recording)", ret, (unsigned)s_cap_read_fail);
            }
            vTaskDelay(pdMS_TO_TICKS(5));
            continue;
        }
        s_cap_read_fail = 0;
        if (s_pcm_cap == 0 || s_pcm_len + (size_t)want > s_pcm_cap) {
            ESP_LOGW(TAG, "buffer full, stopping capture");
            s_recording = false;
            continue;
        }
        size_t cur = s_pcm_len;
        memcpy(s_pcm + cur, chunk, (size_t)want);
        s_pcm_len = cur + (size_t)want;
    }
}

static bool alloc_pcm_ring(void)
{
    s_pcm_cap = 0;
#if CONFIG_SPIRAM
    const size_t internal_pcm_max = (size_t)BOARD_RECORD_MAX_BYTES;
#else
    /* 无 PSRAM：大块 malloc 会耗尽内部堆，LVGL DMA 双缓冲(~150KB+) 分配失败会 NULL 进 lv_disp → 崩溃重启 */
    const size_t internal_pcm_max = (160 * 1024);
#endif

    for (size_t want = BOARD_RECORD_MAX_BYTES; want >= BOARD_RECORD_MIN_BYTES; want /= 2) {
        s_pcm = NULL;
#if CONFIG_SPIRAM
        s_pcm = (uint8_t *)heap_caps_malloc(want, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
#endif
        if (!s_pcm && want <= internal_pcm_max) {
            s_pcm = (uint8_t *)malloc(want);
        }
        if (s_pcm) {
            s_pcm_cap = want;
            ESP_LOGI(TAG, "pcm buffer %u B (max segment ~%u s @16k mono)",
                     (unsigned)want, (unsigned)(want / 32000u));
            return true;
        }
#if !CONFIG_SPIRAM
        if (want > internal_pcm_max) {
            ESP_LOGD(TAG, "pcm skip %u B (no SPIRAM; internal cap %u for LVGL/WiFi)",
                     (unsigned)want, (unsigned)internal_pcm_max);
            continue;
        }
#endif
        ESP_LOGW(TAG, "pcm alloc %u B failed, retry smaller", (unsigned)want);
    }
    ESP_LOGE(TAG, "pcm buffer alloc failed (need PSRAM or free internal heap >= %u B)",
             (unsigned)BOARD_RECORD_MIN_BYTES);
    return false;
}

static void release_pcm_buffer(void)
{
    if (s_pcm) {
        heap_caps_free(s_pcm);
        s_pcm = NULL;
    }
    s_pcm_cap = 0;
}

void app_audio_init(void)
{
    if (s_codec) {
        return;
    }

    if (!alloc_pcm_ring()) {
        return;
    }

    if (init_es8311() != ESP_OK) {
        ESP_LOGE(TAG, "ES8311 init failed");
        release_pcm_buffer();
        return;
    }

    s_cap_task = NULL;
    BaseType_t t = xTaskCreatePinnedToCore(capture_task, "audio_cap", 6144, NULL, 5, &s_cap_task, 1);
    if (t != pdPASS) {
        ESP_LOGW(TAG, "audio_cap on core1 failed, retry any core");
        t = xTaskCreate(capture_task, "audio_cap", 6144, NULL, 5, &s_cap_task);
    }
    if (t != pdPASS) {
        ESP_LOGE(TAG, "audio_cap task create failed — no mic capture (heap?)");
        s_cap_task = NULL;
        esp_codec_dev_delete(s_codec);
        s_codec = NULL;
        release_pcm_buffer();
        return;
    }
    ESP_LOGI(TAG, "audio capture task started chunk=%dB", AUDIO_CAP_CHUNK_BYTES);
}

bool app_audio_is_ready(void)
{
    return s_codec != NULL && s_cap_task != NULL;
}

bool app_audio_recording(void)
{
    return s_recording;
}

void app_audio_record_set(bool on)
{
    if (on && !s_recording) {
        if (!app_audio_is_ready()) {
            ESP_LOGW(TAG, "record start ignored: codec/task not ready");
            return;
        }
        s_pcm_len = 0;
        s_cap_read_fail = 0;
        s_recording = true;
        ESP_LOGI(TAG, "record start");
    } else if (!on && s_recording) {
        s_recording = false;
        ESP_LOGI(TAG, "record stop, bytes=%u", (unsigned)s_pcm_len);
    }
}

esp_err_t app_audio_flush_upload_recording(void)
{
    return flush_upload_if_allowed();
}

void app_audio_record_toggle(void)
{
    app_audio_record_set(!s_recording);
}

size_t app_audio_pcm_bytes(void)
{
    return s_pcm_len;
}

const uint8_t *app_audio_pcm_data(void)
{
    return s_pcm;
}

static void play_tone_ms(uint16_t freq_hz, unsigned dur_ms, float amp)
{
    if (!s_codec) {
        return;
    }
    const unsigned sr = BOARD_AUDIO_SAMPLE_RATE;
    unsigned samples = sr * dur_ms / 1000;
    if (samples < 8) {
        samples = 8;
    }

    int16_t *pcm = (int16_t *)malloc(samples * sizeof(int16_t));
    if (!pcm) {
        return;
    }
    for (unsigned i = 0; i < samples; i++) {
        pcm[i] = (int16_t)(amp * sinf(2.0f * (float)M_PI * (float)freq_hz * (float)i / (float)sr));
    }

    size_t bytes = samples * sizeof(int16_t);
    size_t off = 0;
    while (off < bytes) {
        int chunk = (int)((bytes - off) > 512 ? 512 : (bytes - off));
        int w = esp_codec_dev_write(s_codec, (uint8_t *)pcm + off, chunk);
        if (w != ESP_CODEC_DEV_OK) {
            ESP_LOGW(TAG, "tone write ret=%d", w);
            break;
        }
        off += (size_t)chunk;
    }
    free(pcm);
}

static bool wav_parse_pcm16_mono(const uint8_t *wav, size_t wav_len, const uint8_t **pcm_out,
                                 size_t *pcm_bytes_out, uint32_t *rate_out)
{
    if (!wav || wav_len < 44) {
        return false;
    }
    if (memcmp(wav, "RIFF", 4) != 0 || memcmp(wav + 8, "WAVE", 4) != 0) {
        return false;
    }
    size_t pos = 12;
    bool have_fmt = false;
    uint16_t audio_format = 0;
    uint16_t num_channels = 0;
    uint16_t bits_per_sample = 0;
    uint32_t sample_rate = 0;
    const uint8_t *data_ptr = NULL;
    uint32_t data_size = 0;

    while (pos + 8 <= wav_len) {
        const uint8_t *id = wav + pos;
        uint32_t chunk_sz =
            (uint32_t)id[4] | ((uint32_t)id[5] << 8) | ((uint32_t)id[6] << 16) | ((uint32_t)id[7] << 24);
        pos += 8;
        if (chunk_sz > wav_len || pos > wav_len - chunk_sz) {
            break;
        }
        if (memcmp(id, "fmt ", 4) == 0 && chunk_sz >= 16) {
            const uint8_t *f = wav + pos;
            audio_format = (uint16_t)(f[0] | (f[1] << 8));
            num_channels = (uint16_t)(f[2] | (f[3] << 8));
            sample_rate = (uint32_t)(f[4] | (f[5] << 8) | (f[6] << 16) | (f[7] << 24));
            bits_per_sample = (uint16_t)(f[14] | (f[15] << 8));
            have_fmt = true;
        } else if (memcmp(id, "data", 4) == 0) {
            data_ptr = wav + pos;
            data_size = chunk_sz;
        }
        pos += chunk_sz;
    }

    if (!have_fmt || !data_ptr || data_size == 0) {
        return false;
    }
    if (audio_format != 1 || bits_per_sample != 16 || num_channels != 1) {
        ESP_LOGW(TAG, "wav need PCM16 mono, got fmt=%u ch=%u bits=%u", (unsigned)audio_format,
                 (unsigned)num_channels, (unsigned)bits_per_sample);
        return false;
    }
    *pcm_out = data_ptr;
    *pcm_bytes_out = data_size;
    *rate_out = sample_rate;
    return true;
}

void app_audio_play_wav(const uint8_t *wav, size_t wav_len)
{
    if (!s_codec || !wav || wav_len < 44) {
        return;
    }
    const uint8_t *pcm = NULL;
    size_t pcm_bytes = 0;
    uint32_t sr = 0;
    if (!wav_parse_pcm16_mono(wav, wav_len, &pcm, &pcm_bytes, &sr)) {
        ESP_LOGW(TAG, "wav parse failed");
        return;
    }
    if (sr != BOARD_AUDIO_SAMPLE_RATE) {
        ESP_LOGW(TAG, "wav sr=%u expect %u", (unsigned)sr, (unsigned)BOARD_AUDIO_SAMPLE_RATE);
    }

    aiw_speaker_amp_set(true);
    vTaskDelay(pdMS_TO_TICKS(20));

    size_t off = 0;
    while (off < pcm_bytes) {
        int chunk = (int)((pcm_bytes - off) > 512 ? 512 : (pcm_bytes - off));
        int wr = esp_codec_dev_write(s_codec, (void *)(pcm + off), chunk);
        if (wr != ESP_CODEC_DEV_OK) {
            ESP_LOGW(TAG, "wav write ret=%d", wr);
            break;
        }
        off += (size_t)chunk;
    }
}

void app_audio_play_boot_chime(void)
{
    if (!s_codec) {
        ESP_LOGW(TAG, "boot chime: codec not ready");
        return;
    }
    aiw_speaker_amp_set(true);
    vTaskDelay(pdMS_TO_TICKS(40));

    play_tone_ms(523, 140, 3800.0f);
    vTaskDelay(pdMS_TO_TICKS(60));
    play_tone_ms(784, 180, 4200.0f);

    ESP_LOGI(TAG, "boot chime done");
}
