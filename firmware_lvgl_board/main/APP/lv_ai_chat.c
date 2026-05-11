#include "lv_ai_chat.h"
#include "sdkconfig.h"
#include "app_http.h"
#include "app_wifi.h"
#include "app_audio_codec.h"
#include "board_config.h"
#include "ltdc.h"
#include "cJSON.h"
#include "esp_log.h"
#include "esp_err.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "lvgl.h"
#include "nvs.h"

#include <stdlib.h>
#include <string.h>

static bool voice_cloud_disabled_by_privacy(void)
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
    return privacy;
}

static const char *TAG = "lv_ai_chat";

#define COLOR_SCR       0xE8EEF5
#define COLOR_CARD      0xFFFFFF
#define COLOR_BORDER    0xC5D0E0
#define COLOR_TEXT      0x1E293B
#define COLOR_TEXT_MUTE 0x475569
#define COLOR_BTN_IDLE  0x2563EB
#define COLOR_BTN_REC   0xDC2626
#define COLOR_TOP_BAR   0xDCE8F5

static lv_obj_t *s_log_scroll;
static lv_obj_t *s_log_label;
static lv_obj_t *s_hold_btn;
static lv_obj_t *s_hold_lbl;
static lv_obj_t *s_hint;
static lv_obj_t *s_status;
static lv_obj_t *s_title;

static char s_log[6000];
static bool s_op_busy;
static uint32_t s_press_tick;
static lv_timer_t *s_meter;

#define MIN_HOLD_MS 220
#define MIN_PCM_BYTES 2048
#define VOICE_JSON_CAP 16384

static void meter_cb(lv_timer_t *tm);

static const char k_hint_idle[] =
    "Hold to speak | Release: voice -> DeepSeek reply";

typedef struct {
    esp_err_t err;
    char msg[320];
    /** 识别文本；仅在 LVGL 线程通过 chat_append 显示，勿在工作线程调 LVGL */
    char user_transcript[384];
} voice_chat_result_t;

static void upload_fail_explain(esp_err_t err, char *buf, size_t len)
{
    const char *base = app_http_base_url();
    switch (err) {
    case ESP_ERR_NO_MEM:
        snprintf(buf, len, "NO_MEM: reboot or shorter clip");
        break;
    case ESP_FAIL:
        snprintf(buf, len, "HTTP_FAIL server=%s", base);
        break;
    case ESP_ERR_INVALID_ARG:
        snprintf(buf, len, "Bad URL in menuconfig");
        break;
    default:
        snprintf(buf, len, "%s Config=%s", esp_err_to_name(err), base);
        break;
    }
}

static void apply_font_cjk(lv_obj_t *obj)
{
#if defined(CONFIG_LV_FONT_SIMSUN_16_CJK) && CONFIG_LV_FONT_SIMSUN_16_CJK
    lv_obj_set_style_text_font(obj, &lv_font_simsun_16_cjk, LV_PART_MAIN);
#elif defined(CONFIG_LV_FONT_DEFAULT_MONTSERRAT_14) && CONFIG_LV_FONT_DEFAULT_MONTSERRAT_14
    lv_obj_set_style_text_font(obj, &lv_font_montserrat_14, LV_PART_MAIN);
#else
    lv_obj_set_style_text_font(obj, &lv_font_montserrat_14, LV_PART_MAIN);
#endif
}

static void style_body_text(lv_obj_t *obj)
{
    apply_font_cjk(obj);
    lv_obj_set_style_text_color(obj, lv_color_hex(COLOR_TEXT), LV_PART_MAIN);
}

static void chat_append(const char *who, const char *text)
{
    size_t n = strlen(s_log);
    if (n > sizeof(s_log) * 2 / 3) {
        const size_t drop = sizeof(s_log) / 3;
        if (drop < n) {
            memmove(s_log, s_log + drop, n - drop + 1);
            n = strlen(s_log);
        }
    }
    snprintf(s_log + n, sizeof(s_log) - n, "[%s] %s\n", who, text);
    lv_label_set_text(s_log_label, s_log);
    lv_obj_scroll_to_view(s_log_label, LV_ANIM_OFF);
}

static void voice_apply_async(void *p)
{
    voice_chat_result_t *r = (voice_chat_result_t *)p;
    if (r->err == ESP_OK) {
        if (r->user_transcript[0]) {
            chat_append("You", r->user_transcript);
        }
        chat_append("AI", r->msg[0] ? r->msg : "(empty)");
        ESP_LOGI(TAG, "voice chat ok");
    } else {
        chat_append("!", r->msg[0] ? r->msg : esp_err_to_name(r->err));
        ESP_LOGW(TAG, "voice chat err %s", esp_err_to_name(r->err));
    }
    s_op_busy = false;
    lv_obj_clear_state(s_hold_btn, LV_STATE_DISABLED);
    lv_label_set_text(s_hint, k_hint_idle);
    lv_obj_set_style_bg_color(s_hold_btn, lv_color_hex(COLOR_BTN_IDLE), LV_PART_MAIN);
    free(r);
}

static void voice_oom_async(void *p)
{
    (void)p;
    lv_obj_clear_state(s_hold_btn, LV_STATE_DISABLED);
    lv_label_set_text(s_hint, k_hint_idle);
    lv_obj_set_style_bg_color(s_hold_btn, lv_color_hex(COLOR_BTN_IDLE), LV_PART_MAIN);
    chat_append("!", "OOM");
    s_op_busy = false;
}

/** 解析 /api/chat/from_wav 成功或失败 JSON */
static void parse_voice_response(const char *json, esp_err_t http_err, voice_chat_result_t *out)
{
    out->msg[0] = '\0';
    out->user_transcript[0] = '\0';
    if (!json || !json[0]) {
        if (http_err != ESP_OK) {
            upload_fail_explain(http_err, out->msg, sizeof(out->msg));
        }
        out->err = http_err != ESP_OK ? http_err : ESP_FAIL;
        return;
    }

    cJSON *root = cJSON_Parse(json);
    if (!root) {
        snprintf(out->msg, sizeof(out->msg), "Bad JSON (server?)");
        out->err = ESP_FAIL;
        return;
    }

    const cJSON *err_item = cJSON_GetObjectItem(root, "error");
    if (cJSON_IsString(err_item) && err_item->valuestring && err_item->valuestring[0]) {
        strncpy(out->msg, err_item->valuestring, sizeof(out->msg) - 1);
        out->msg[sizeof(out->msg) - 1] = '\0';
        out->err = ESP_FAIL;
        cJSON_Delete(root);
        return;
    }

    const cJSON *tr = cJSON_GetObjectItem(root, "transcript");
    const cJSON *rep = cJSON_GetObjectItem(root, "reply");
    if (http_err != ESP_OK) {
        upload_fail_explain(http_err, out->msg, sizeof(out->msg));
        out->err = http_err;
        cJSON_Delete(root);
        return;
    }

    if (cJSON_IsString(rep) && rep->valuestring) {
        if (cJSON_IsString(tr) && tr->valuestring && tr->valuestring[0]) {
            strncpy(out->user_transcript, tr->valuestring, sizeof(out->user_transcript) - 1);
            out->user_transcript[sizeof(out->user_transcript) - 1] = '\0';
        }
        strncpy(out->msg, rep->valuestring, sizeof(out->msg) - 1);
        out->msg[sizeof(out->msg) - 1] = '\0';
        out->err = ESP_OK;
    } else {
        snprintf(out->msg, sizeof(out->msg), "No reply field");
        out->err = ESP_FAIL;
    }
    cJSON_Delete(root);
}

static void voice_chat_worker(void *arg)
{
    (void)arg;
    vTaskDelay(pdMS_TO_TICKS(260));

    voice_chat_result_t *r = (voice_chat_result_t *)calloc(1, sizeof(voice_chat_result_t));
    if (!r) {
        ESP_LOGE(TAG, "voice_chat_result OOM");
        s_op_busy = false;
        lv_async_call(voice_oom_async, NULL);
        vTaskDelete(NULL);
        return;
    }

    size_t n = app_audio_pcm_bytes();
    const uint8_t *pcm = app_audio_pcm_data();
    if (!pcm || n < MIN_PCM_BYTES) {
        r->err = ESP_ERR_INVALID_SIZE;
        snprintf(r->msg, sizeof(r->msg), "PCM %uB too short", (unsigned)n);
        lv_async_call(voice_apply_async, r);
        vTaskDelete(NULL);
        return;
    }

    if (voice_cloud_disabled_by_privacy()) {
        r->err = ESP_ERR_NOT_SUPPORTED;
        snprintf(r->msg, sizeof(r->msg), "Privacy on (no cloud)");
        lv_async_call(voice_apply_async, r);
        vTaskDelete(NULL);
        return;
    }

    char *json_buf = (char *)malloc(VOICE_JSON_CAP);
    if (!json_buf) {
        r->err = ESP_ERR_NO_MEM;
        upload_fail_explain(r->err, r->msg, sizeof(r->msg));
        lv_async_call(voice_apply_async, r);
        vTaskDelete(NULL);
        return;
    }

    esp_err_t err = app_http_post_pcm_voice_chat(pcm, n, BOARD_AUDIO_SAMPLE_RATE, json_buf, VOICE_JSON_CAP);
    parse_voice_response(json_buf, err, r);
    free(json_buf);

    if (r->err == ESP_OK && r->msg[0] && app_audio_is_ready()) {
        uint8_t *wav = NULL;
        size_t wav_len = 0;
        esp_err_t terr = app_http_fetch_tts_wav(r->msg, &wav, &wav_len);
        if (terr == ESP_OK && wav) {
            app_audio_play_wav(wav, wav_len);
        } else if (terr != ESP_OK) {
            ESP_LOGW(TAG, "tts fetch %s", esp_err_to_name(terr));
        }
        free(wav);
    }

    lv_async_call(voice_apply_async, r);
    vTaskDelete(NULL);
}

static void meter_cb(lv_timer_t *tm)
{
    (void)tm;
    if (!app_audio_recording()) {
        if (s_meter) {
            lv_timer_del(s_meter);
            s_meter = NULL;
        }
        return;
    }
    char b[48];
    snprintf(b, sizeof(b), "REC %u B", (unsigned)app_audio_pcm_bytes());
    lv_label_set_text(s_hint, b);
}

static void hold_event(lv_event_t *e)
{
    lv_event_code_t code = lv_event_get_code(e);
    if (code == LV_EVENT_PRESSED) {
        if (s_op_busy) {
            return;
        }
        if (!app_audio_is_ready()) {
            lv_label_set_text(s_hint, "Audio not ready");
            return;
        }
        s_press_tick = lv_tick_get();
        app_audio_record_set(true);
        if (s_meter == NULL) {
            s_meter = lv_timer_create(meter_cb, 100, NULL);
        }
        lv_label_set_text(s_hint, "Recording...");
        lv_obj_set_style_bg_color(s_hold_btn, lv_color_hex(COLOR_BTN_REC), LV_PART_MAIN);
        return;
    }

    if (code == LV_EVENT_RELEASED || code == LV_EVENT_PRESS_LOST) {
        if (s_meter) {
            lv_timer_del(s_meter);
            s_meter = NULL;
        }
        if (!app_audio_recording()) {
            return;
        }
        app_audio_record_set(false);
        lv_obj_set_style_bg_color(s_hold_btn, lv_color_hex(COLOR_BTN_IDLE), LV_PART_MAIN);

        uint32_t ms = lv_tick_elaps(s_press_tick);
        size_t pcm_n = app_audio_pcm_bytes();
        ESP_LOGI(TAG, "release after %ums, pcm=%u B", (unsigned)ms, (unsigned)pcm_n);
        if (ms < MIN_HOLD_MS) {
            lv_label_set_text(s_hint, "Hold a bit longer");
            return;
        }

        if (s_op_busy) {
            return;
        }
        s_op_busy = true;
        lv_obj_add_state(s_hold_btn, LV_STATE_DISABLED);
        lv_label_set_text(s_hint, "Thinking...");

        BaseType_t ok = xTaskCreate(voice_chat_worker, "voice_ai", 24576, NULL, 5, NULL);
        if (ok != pdPASS) {
            s_op_busy = false;
            lv_obj_clear_state(s_hold_btn, LV_STATE_DISABLED);
            lv_label_set_text(s_hint, "task fail");
            chat_append("!", "task");
        }
    }
}

void lv_ai_chat_init(void)
{
    s_op_busy = false;
    lv_obj_t *scr = lv_scr_act();
    lv_obj_set_style_bg_color(scr, lv_color_hex(COLOR_SCR), LV_PART_MAIN);

    s_title = lv_label_create(scr);
    style_body_text(s_title);
    lv_obj_set_style_text_color(s_title, lv_color_hex(COLOR_TEXT), LV_PART_MAIN);
    lv_label_set_text(s_title, "Ai Watch");
    lv_obj_align(s_title, LV_ALIGN_TOP_LEFT, 8, 6);

    s_status = lv_label_create(scr);
    style_body_text(s_status);
    lv_obj_set_style_text_color(s_status, lv_color_hex(COLOR_TEXT_MUTE), LV_PART_MAIN);
    lv_label_set_long_mode(s_status, LV_LABEL_LONG_SCROLL_CIRCULAR);
    lv_obj_set_width(s_status, lcddev.width - 16);
    {
        char buf[120];
        snprintf(buf, sizeof(buf), "%s  |  %s", app_wifi_ip_str(), app_http_base_url());
        lv_label_set_text(s_status, buf);
    }
    lv_obj_align(s_status, LV_ALIGN_TOP_MID, 0, 26);

    int top_h = 52;
    int hold_h = (int)(lcddev.height * 0.20f);
    if (hold_h < 80) {
        hold_h = 80;
    }
    if (hold_h > 140) {
        hold_h = 140;
    }
    int log_h = lcddev.height - top_h - hold_h - 48;
    if (log_h < 80) {
        log_h = 80;
    }

    s_log_scroll = lv_obj_create(scr);
    lv_obj_set_size(s_log_scroll, lcddev.width - 12, log_h);
    lv_obj_align(s_log_scroll, LV_ALIGN_TOP_MID, 0, top_h);
    lv_obj_set_style_bg_color(s_log_scroll, lv_color_hex(COLOR_CARD), LV_PART_MAIN);
    lv_obj_set_style_border_color(s_log_scroll, lv_color_hex(COLOR_BORDER), LV_PART_MAIN);
    lv_obj_set_style_border_width(s_log_scroll, 1, LV_PART_MAIN);
    lv_obj_set_style_radius(s_log_scroll, 10, LV_PART_MAIN);
    lv_obj_set_style_pad_all(s_log_scroll, 8, LV_PART_MAIN);
    lv_obj_set_scroll_dir(s_log_scroll, LV_DIR_VER);

    s_log_label = lv_label_create(s_log_scroll);
    lv_label_set_long_mode(s_log_label, LV_LABEL_LONG_WRAP);
    lv_obj_set_width(s_log_label, lcddev.width - 36);
    style_body_text(s_log_label);
    s_log[0] = '\0';
    snprintf(s_log, sizeof(s_log),
             "[Tip] Hold button, speak, release. PC runs STT + DeepSeek. /desk for settings.\n");
    lv_label_set_text(s_log_label, s_log);

    s_hint = lv_label_create(scr);
    style_body_text(s_hint);
    lv_obj_set_style_text_color(s_hint, lv_color_hex(COLOR_TEXT_MUTE), LV_PART_MAIN);
    lv_obj_set_width(s_hint, lcddev.width - 12);
    lv_label_set_long_mode(s_hint, LV_LABEL_LONG_WRAP);
    lv_label_set_text(s_hint, k_hint_idle);
    lv_obj_align(s_hint, LV_ALIGN_BOTTOM_MID, 0, -hold_h - 34);

    s_hold_btn = lv_btn_create(scr);
    lv_obj_set_size(s_hold_btn, lcddev.width - 20, hold_h);
    lv_obj_align(s_hold_btn, LV_ALIGN_BOTTOM_MID, 0, -10);
    lv_obj_set_style_radius(s_hold_btn, 16, LV_PART_MAIN);
    lv_obj_set_style_bg_color(s_hold_btn, lv_color_hex(COLOR_BTN_IDLE), LV_PART_MAIN);
    lv_obj_set_style_shadow_width(s_hold_btn, 8, LV_PART_MAIN);
    lv_obj_set_style_shadow_ofs_y(s_hold_btn, 3, LV_PART_MAIN);
    lv_obj_set_style_shadow_opa(s_hold_btn, LV_OPA_30, LV_PART_MAIN);
    lv_obj_add_event_cb(s_hold_btn, hold_event, LV_EVENT_ALL, NULL);

    s_hold_lbl = lv_label_create(s_hold_btn);
    apply_font_cjk(s_hold_lbl);
    lv_obj_set_style_text_color(s_hold_lbl, lv_color_hex(0xFFFFFF), LV_PART_MAIN);
    lv_label_set_text(s_hold_lbl, "Hold to talk");
    lv_obj_center(s_hold_lbl);
    lv_obj_clear_flag(s_hold_lbl, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_flag(s_hold_lbl, LV_OBJ_FLAG_EVENT_BUBBLE);

    lv_obj_move_foreground(s_hold_btn);
}
