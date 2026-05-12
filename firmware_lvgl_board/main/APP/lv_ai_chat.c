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
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "lvgl.h"
#include "nvs.h"

#include <stdlib.h>
#include <string.h>
#include <time.h>

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

#define COLOR_SCR_BG    0xC5D3EC
#define COLOR_HEADER_BG 0xE8EDF7
#define COLOR_CARD      0xFFFFFF
#define COLOR_BORDER    0x94A3B8
#define COLOR_TEXT      0x1E293B
#define COLOR_TEXT_MUTE 0x64748B
#define COLOR_ACCENT    0x6366F1
#define COLOR_CHAT_BTN  0x6366F1
#define COLOR_CHAT_HOLD 0xF59E0B
#define COLOR_REC_IDLE  0x64748B
#define COLOR_REC_ON    0xEF4444
#define COLOR_REC_ON2   0xDC2626

#define VOICE_JSON_CAP     16384
#define BTN_ROW_H          48
#define BTN_PAD            6
#define MIN_CHAT_PCM_BYTES 8000
#define STREAM_TIMER_MS    280
#define UPLOAD_CHUNK       16384

static lv_obj_t *s_header;
static lv_obj_t *s_wifi_glyph;
static lv_obj_t *s_title_glyph;
static lv_obj_t *s_clock;
static lv_obj_t *s_title;
static lv_obj_t *s_status;
static lv_obj_t *s_log_scroll;
static lv_obj_t *s_log_label;
static lv_obj_t *s_hint_row;
static lv_obj_t *s_hint;
#if LV_USE_SPINNER
static lv_obj_t *s_spinner;
#endif
static lv_obj_t *s_btn_chat;
static lv_obj_t *s_btn_rec;
static lv_obj_t *s_lbl_chat;
static lv_obj_t *s_lbl_rec;

static char s_log[6000];
static bool s_op_busy;
static lv_timer_t *s_meter;
static lv_timer_t *s_rec_anim;
static lv_timer_t *s_clock_timer;
static lv_timer_t *s_think_anim;
static uint8_t s_think_phase;

/** 左侧：长按已开始采音，松手后发 STT+AI */
static bool s_chat_hold_active;
/** 右侧：服务台录音，第一次点击后采集中 */
static bool s_desk_rec_active;

static char s_desk_stream_sid[48];
static volatile bool s_desk_stream_stop_req;
static TaskHandle_t s_desk_stream_task;
static lv_timer_t *s_desk_stream_timer;

/** 录音开始时刻（esp_timer 微秒）；与 s_rec_timing 配合用于时长 */
static uint64_t s_rec_start_us;
static bool s_rec_timing;

static const char k_hint_idle[] = "Left: hold AI | Right: desk stream→/desk";

typedef struct {
    esp_err_t err;
    char msg[320];
    char user_transcript[384];
} voice_chat_result_t;

typedef struct {
    esp_err_t er;
    char detail[192];
} desk_upload_result_t;

static void upload_fail_explain(esp_err_t err, char *buf, size_t len)
{
    const char *base = app_http_base_url();
    switch (err) {
    case ESP_ERR_NO_MEM:
        snprintf(buf, len, "No memory");
        break;
    case ESP_FAIL:
        snprintf(buf, len, "HTTP (%s)", base);
        break;
    case ESP_ERR_INVALID_ARG:
        snprintf(buf, len, "Bad URL");
        break;
    case ESP_ERR_INVALID_SIZE:
        snprintf(buf, len, "Too short");
        break;
    case ESP_ERR_NOT_SUPPORTED:
        snprintf(buf, len, "Privacy on");
        break;
    default:
        snprintf(buf, len, "%s", esp_err_to_name(err));
        break;
    }
}

static void apply_font_ui(lv_obj_t *obj)
{
#if CONFIG_LV_FONT_MONTSERRAT_14
    lv_obj_set_style_text_font(obj, &lv_font_montserrat_14, LV_PART_MAIN);
#elif CONFIG_LV_FONT_MONTSERRAT_18
    lv_obj_set_style_text_font(obj, &lv_font_montserrat_18, LV_PART_MAIN);
#endif
}

static void apply_font_chat(lv_obj_t *obj)
{
#if CONFIG_LV_FONT_SIMSUN_16_CJK
    lv_obj_set_style_text_font(obj, &lv_font_simsun_16_cjk, LV_PART_MAIN);
#elif CONFIG_LV_FONT_MONTSERRAT_14
    lv_obj_set_style_text_font(obj, &lv_font_montserrat_14, LV_PART_MAIN);
#elif CONFIG_LV_FONT_MONTSERRAT_18
    lv_obj_set_style_text_font(obj, &lv_font_montserrat_18, LV_PART_MAIN);
#endif
}

static void apply_font_symbols(lv_obj_t *obj)
{
#if CONFIG_LV_FONT_MONTSERRAT_18
    lv_obj_set_style_text_font(obj, &lv_font_montserrat_18, LV_PART_MAIN);
#elif CONFIG_LV_FONT_MONTSERRAT_14
    lv_obj_set_style_text_font(obj, &lv_font_montserrat_14, LV_PART_MAIN);
#endif
}

static void style_ui_text(lv_obj_t *obj)
{
    apply_font_ui(obj);
    lv_obj_set_style_text_color(obj, lv_color_hex(COLOR_TEXT), LV_PART_MAIN);
}

static void style_chat_text(lv_obj_t *obj)
{
    apply_font_chat(obj);
    lv_obj_set_style_text_color(obj, lv_color_hex(COLOR_TEXT), LV_PART_MAIN);
}

static void wall_clock_refresh(void)
{
    if (!s_clock) {
        return;
    }
    if (!app_wifi_time_synced()) {
        lv_label_set_text(s_clock, "--:--");
        return;
    }
    time_t t = time(NULL);
    struct tm lt;
    if (localtime_r(&t, &lt) == NULL) {
        return;
    }
    char b[8];
    (void)snprintf(b, sizeof(b), "%02d:%02d", lt.tm_hour, lt.tm_min);
    lv_label_set_text(s_clock, b);
}

static void clock_timer_cb(lv_timer_t *tm)
{
    (void)tm;
    wall_clock_refresh();
}

static void lv_wall_refresh_async(void *p)
{
    (void)p;
    wall_clock_refresh();
}

static void app_wifi_time_bridge(void *user)
{
    (void)user;
    lv_async_call(lv_wall_refresh_async, NULL);
}

static void rec_hint_show_elapsed(void)
{
    if (!s_rec_timing || !app_audio_recording()) {
        return;
    }
    uint64_t d_us = esp_timer_get_time() - s_rec_start_us;
    unsigned sec = (unsigned)(d_us / 1000000ULL);
    unsigned h = sec / 3600U;
    unsigned m = (sec % 3600U) / 60U;
    unsigned s = sec % 60U;
    char b[32];
    if (h > 0U) {
        snprintf(b, sizeof(b), "REC %u:%02u:%02u", h, m, s);
    } else {
        snprintf(b, sizeof(b), "REC %u:%02u", m, s);
    }
    lv_label_set_text(s_hint, b);
}

static const char *pretty_who(const char *who)
{
    if (!strcmp(who, "You")) {
        return "You";
    }
    if (!strcmp(who, "AI")) {
        return "AI";
    }
    if (!strcmp(who, "Tip")) {
        return "Tip";
    }
    return "!";
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
    snprintf(s_log + n, sizeof(s_log) - n, "[%s] %s\n\n", pretty_who(who), text);
    lv_label_set_text(s_log_label, s_log);
    lv_obj_scroll_to_view(s_log_label, LV_ANIM_ON);
}

static void chat_btn_style_idle(void)
{
    lv_obj_set_style_bg_grad_dir(s_btn_chat, LV_GRAD_DIR_NONE, LV_PART_MAIN);
    lv_obj_set_style_bg_color(s_btn_chat, lv_color_hex(COLOR_CHAT_BTN), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(s_btn_chat, LV_OPA_COVER, LV_PART_MAIN);
}

static void chat_btn_style_hold(void)
{
    lv_obj_set_style_bg_grad_dir(s_btn_chat, LV_GRAD_DIR_NONE, LV_PART_MAIN);
    lv_obj_set_style_bg_color(s_btn_chat, lv_color_hex(COLOR_CHAT_HOLD), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(s_btn_chat, LV_OPA_COVER, LV_PART_MAIN);
}

static void rec_btn_style_idle(void)
{
    lv_obj_set_style_bg_grad_dir(s_btn_rec, LV_GRAD_DIR_NONE, LV_PART_MAIN);
    lv_obj_set_style_bg_color(s_btn_rec, lv_color_hex(COLOR_REC_IDLE), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(s_btn_rec, LV_OPA_COVER, LV_PART_MAIN);
}

static void rec_btn_style_rec(void)
{
    lv_obj_set_style_bg_color(s_btn_rec, lv_color_hex(COLOR_REC_ON), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(s_btn_rec, LV_OPA_COVER, LV_PART_MAIN);
}

static void rec_anim_stop(void)
{
    if (s_rec_anim) {
        lv_timer_del(s_rec_anim);
        s_rec_anim = NULL;
    }
}

static void rec_anim_cb(lv_timer_t *tm)
{
    (void)tm;
    if (!s_desk_rec_active || !app_audio_recording()) {
        rec_anim_stop();
        return;
    }
    static uint8_t ph;
    ph++;
    if (ph & 1) {
        rec_btn_style_rec();
    } else {
        lv_obj_set_style_bg_color(s_btn_rec, lv_color_hex(COLOR_REC_ON2), LV_PART_MAIN);
    }
}

static void rec_anim_start(void)
{
    if (s_rec_anim) {
        return;
    }
    s_rec_anim = lv_timer_create(rec_anim_cb, 320, NULL);
}

static void thinking_ui_stop(void)
{
    if (s_think_anim) {
        lv_timer_del(s_think_anim);
        s_think_anim = NULL;
    }
    s_think_phase = 0;
#if LV_USE_SPINNER
    if (s_spinner) {
        lv_obj_add_flag(s_spinner, LV_OBJ_FLAG_HIDDEN);
    }
#endif
    lv_obj_set_style_opa(s_hint, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_align(s_hint, LV_ALIGN_LEFT_MID, 4, 0);
}

static void think_anim_cb(lv_timer_t *tm)
{
    (void)tm;
    static const char *dots[] = {"", ".", "..", "..."};
    char buf[48];
    snprintf(buf, sizeof(buf), "Thinking%s", dots[s_think_phase % 4]);
    s_think_phase++;
    lv_label_set_text(s_hint, buf);
}

static void thinking_ui_start(void)
{
    thinking_ui_stop();
#if LV_USE_SPINNER
    if (s_spinner) {
        lv_obj_clear_flag(s_spinner, LV_OBJ_FLAG_HIDDEN);
        lv_obj_align_to(s_hint, s_spinner, LV_ALIGN_OUT_RIGHT_MID, 8, 0);
    }
#endif
    lv_label_set_text(s_hint, "Thinking");
    s_think_anim = lv_timer_create(think_anim_cb, 380, NULL);
}

static void voice_apply_async(void *p)
{
    voice_chat_result_t *r = (voice_chat_result_t *)p;
    thinking_ui_stop();
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
    lv_obj_clear_state(s_btn_chat, LV_STATE_DISABLED);
    if (!s_desk_rec_active) {
        lv_obj_clear_state(s_btn_rec, LV_STATE_DISABLED);
    }
    lv_label_set_text(s_hint, k_hint_idle);
    chat_btn_style_idle();
    if (!s_desk_rec_active) {
        rec_btn_style_idle();
    }
    free(r);
}

static void voice_oom_async(void *p)
{
    (void)p;
    thinking_ui_stop();
    s_op_busy = false;
    lv_obj_clear_state(s_btn_chat, LV_STATE_DISABLED);
    if (!s_desk_rec_active) {
        lv_obj_clear_state(s_btn_rec, LV_STATE_DISABLED);
    }
    lv_label_set_text(s_hint, k_hint_idle);
    chat_btn_style_idle();
    if (!s_desk_rec_active) {
        rec_btn_style_idle();
    }
    chat_append("!", "OOM");
}

static void desk_apply_async(void *p)
{
    desk_upload_result_t *d = (desk_upload_result_t *)p;
    if (!d) {
        s_op_busy = false;
        s_desk_rec_active = false;
        lv_obj_clear_state(s_btn_chat, LV_STATE_DISABLED);
        lv_obj_clear_state(s_btn_rec, LV_STATE_DISABLED);
        lv_label_set_text(s_hint, k_hint_idle);
        rec_btn_style_idle();
        rec_anim_stop();
        chat_append("!", "Desk finish OOM");
        return;
    }
    if (d->er == ESP_OK && d->detail[0]) {
        chat_append("Tip", d->detail);
    } else if (d->er == ESP_OK) {
        chat_append("Tip", "Uploaded — /desk");
    } else {
        chat_append("!", d->detail[0] ? d->detail : esp_err_to_name(d->er));
    }
    s_op_busy = false;
    s_desk_rec_active = false;
    lv_obj_clear_state(s_btn_chat, LV_STATE_DISABLED);
    lv_obj_clear_state(s_btn_rec, LV_STATE_DISABLED);
    lv_label_set_text(s_hint, k_hint_idle);
    rec_btn_style_idle();
    rec_anim_stop();
    free(d);
}

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
        snprintf(out->msg, sizeof(out->msg), "Bad JSON");
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
        snprintf(out->msg, sizeof(out->msg), "No reply");
        out->err = ESP_FAIL;
    }
    cJSON_Delete(root);
}

static void voice_chat_worker(void *arg)
{
    (void)arg;
    vTaskDelay(pdMS_TO_TICKS(80));

    voice_chat_result_t *r = (voice_chat_result_t *)calloc(1, sizeof(voice_chat_result_t));
    if (!r) {
        s_op_busy = false;
        lv_async_call(voice_oom_async, NULL);
        vTaskDelete(NULL);
        return;
    }

    size_t n = app_audio_pcm_bytes();
    if (n < MIN_CHAT_PCM_BYTES) {
        r->err = ESP_ERR_INVALID_SIZE;
        upload_fail_explain(r->err, r->msg, sizeof(r->msg));
        lv_async_call(voice_apply_async, r);
        app_audio_pcm_buffer_reset();
        vTaskDelete(NULL);
        return;
    }

    uint8_t *pcm = (uint8_t *)malloc(n);
    if (!pcm) {
        r->err = ESP_ERR_NO_MEM;
        upload_fail_explain(r->err, r->msg, sizeof(r->msg));
        lv_async_call(voice_apply_async, r);
        vTaskDelete(NULL);
        return;
    }
    memcpy(pcm, app_audio_pcm_data(), n);
    app_audio_pcm_buffer_reset();

    char *json_buf = (char *)malloc(VOICE_JSON_CAP);
    if (!json_buf) {
        free(pcm);
        r->err = ESP_ERR_NO_MEM;
        upload_fail_explain(r->err, r->msg, sizeof(r->msg));
        lv_async_call(voice_apply_async, r);
        vTaskDelete(NULL);
        return;
    }

    esp_err_t err = app_http_post_pcm_voice_chat(pcm, n, BOARD_AUDIO_SAMPLE_RATE, json_buf, VOICE_JSON_CAP);
    free(pcm);
    parse_voice_response(json_buf, err, r);
    free(json_buf);

    if (r->err == ESP_OK && r->msg[0] && app_audio_is_ready()) {
        uint8_t *wav = NULL;
        size_t wav_len = 0;
        esp_err_t terr = app_http_fetch_tts_wav(r->msg, &wav, &wav_len);
        if (terr == ESP_OK && wav) {
            app_audio_play_wav(wav, wav_len);
        }
        free(wav);
    }

    lv_async_call(voice_apply_async, r);
    vTaskDelete(NULL);
}

static void desk_finish_parse(const char *json, esp_err_t http_err, desk_upload_result_t *out)
{
    out->detail[0] = '\0';
    if (http_err != ESP_OK) {
        out->er = http_err;
        upload_fail_explain(http_err, out->detail, sizeof(out->detail));
        return;
    }
    if (!json || !json[0]) {
        out->er = ESP_FAIL;
        snprintf(out->detail, sizeof(out->detail), "Empty response");
        return;
    }
    cJSON *root = cJSON_Parse(json);
    if (!root) {
        out->er = ESP_FAIL;
        snprintf(out->detail, sizeof(out->detail), "Bad JSON");
        return;
    }
    const cJSON *e = cJSON_GetObjectItem(root, "error");
    if (cJSON_IsString(e) && e->valuestring && e->valuestring[0]) {
        snprintf(out->detail, sizeof(out->detail), "%s", e->valuestring);
        out->er = ESP_FAIL;
        cJSON_Delete(root);
        return;
    }
    const cJSON *saved = cJSON_GetObjectItem(root, "saved");
    const cJSON *pb = cJSON_GetObjectItem(root, "pcm_bytes");
    int bytes = cJSON_IsNumber(pb) ? (int)cJSON_GetNumberValue(pb) : 0;
    if (cJSON_IsString(saved) && saved->valuestring) {
        snprintf(out->detail, sizeof(out->detail), "Desk %s (%d B)", saved->valuestring, bytes);
    } else {
        snprintf(out->detail, sizeof(out->detail), "Saved %d B", bytes);
    }
    out->er = ESP_OK;
    cJSON_Delete(root);
}

static void desk_stream_timer_cb(lv_timer_t *tm)
{
    (void)tm;
    if (!s_desk_stream_sid[0] || !s_desk_stream_task) {
        return;
    }
    if (app_audio_pcm_bytes() >= 8192) {
        xTaskNotifyGive(s_desk_stream_task);
    }
}

static void desk_stream_upload_task(void *arg)
{
    (void)arg;
    uint8_t *buf = (uint8_t *)malloc(UPLOAD_CHUNK);
    if (!buf) {
        ESP_LOGE(TAG, "desk stream buf OOM");
        vTaskDelete(NULL);
        return;
    }
    for (;;) {
        ulTaskNotifyTake(pdTRUE, portMAX_DELAY);
        while (s_desk_stream_sid[0] && app_audio_pcm_bytes() >= 8192) {
            size_t got = 0;
            app_audio_pcm_copy_and_consume(buf, UPLOAD_CHUNK, &got);
            if (got == 0) {
                break;
            }
            esp_err_t er = app_http_voice_stream_chunk(s_desk_stream_sid, buf, got);
            if (er != ESP_OK) {
                ESP_LOGW(TAG, "desk chunk %s", esp_err_to_name(er));
            }
        }
        if (s_desk_stream_stop_req && s_desk_stream_sid[0]) {
            while (app_audio_pcm_bytes() > 0) {
                size_t got = 0;
                app_audio_pcm_copy_and_consume(buf, UPLOAD_CHUNK, &got);
                if (got == 0) {
                    break;
                }
                app_http_voice_stream_chunk(s_desk_stream_sid, buf, got);
            }
            char finish_js[384];
            memset(finish_js, 0, sizeof(finish_js));
            esp_err_t fe = app_http_voice_stream_finish(s_desk_stream_sid, false, true, finish_js,
                                                        sizeof(finish_js) - 1);

            desk_upload_result_t *d = (desk_upload_result_t *)calloc(1, sizeof(desk_upload_result_t));
            if (d) {
                desk_finish_parse(finish_js, fe, d);
            }
            s_desk_stream_sid[0] = '\0';
            s_desk_stream_stop_req = false;
            app_audio_pcm_buffer_reset();
            lv_async_call(desk_apply_async, d);
        }
    }
}

static void meter_cb(lv_timer_t *tm)
{
    (void)tm;
    if (!app_audio_recording()) {
        if (s_meter) {
            lv_timer_del(s_meter);
            s_meter = NULL;
        }
        s_rec_timing = false;
        s_rec_start_us = 0;
        return;
    }
    rec_hint_show_elapsed();
}

static void chat_btn_event(lv_event_t *e)
{
    lv_event_code_t code = lv_event_get_code(e);
    if (code == LV_EVENT_LONG_PRESSED) {
        if (s_op_busy || s_desk_rec_active || !app_audio_is_ready()) {
            return;
        }
        if (voice_cloud_disabled_by_privacy()) {
            lv_label_set_text(s_hint, "Privacy on");
            return;
        }
        app_audio_record_set(true);
        s_chat_hold_active = true;
        s_rec_timing = true;
        s_rec_start_us = esp_timer_get_time();
        lv_obj_add_state(s_btn_rec, LV_STATE_DISABLED);
        chat_btn_style_hold();
        if (s_meter == NULL) {
            s_meter = lv_timer_create(meter_cb, 1000, NULL);
        }
        lv_timer_reset(s_meter);
        rec_hint_show_elapsed();
        ESP_LOGI(TAG, "AI hold: record start");
        return;
    }
    if (code != LV_EVENT_RELEASED) {
        return;
    }
    if (!s_chat_hold_active) {
        return;
    }
    s_chat_hold_active = false;
    lv_obj_clear_state(s_btn_rec, LV_STATE_DISABLED);
    app_audio_record_set(false);
    s_rec_timing = false;
    chat_btn_style_idle();
    if (s_meter) {
        lv_timer_del(s_meter);
        s_meter = NULL;
    }

    size_t n = app_audio_pcm_bytes();
    if (n < MIN_CHAT_PCM_BYTES) {
        lv_label_set_text(s_hint, "Too short (hold longer)");
        app_audio_pcm_buffer_reset();
        ESP_LOGW(TAG, "AI hold: pcm too short %u B", (unsigned)n);
        return;
    }

    s_op_busy = true;
    lv_obj_add_state(s_btn_chat, LV_STATE_DISABLED);
    lv_obj_add_state(s_btn_rec, LV_STATE_DISABLED);
    thinking_ui_start();

    BaseType_t ok = xTaskCreate(voice_chat_worker, "voice_chat", 24576, NULL, 5, NULL);
    if (ok != pdPASS) {
        thinking_ui_stop();
        s_op_busy = false;
        lv_obj_clear_state(s_btn_chat, LV_STATE_DISABLED);
        if (!s_desk_rec_active) {
            lv_obj_clear_state(s_btn_rec, LV_STATE_DISABLED);
        }
        lv_label_set_text(s_hint, "Task fail");
        ESP_LOGE(TAG, "voice_chat task create failed");
    }
}

static void record_btn_event(lv_event_t *e)
{
    if (lv_event_get_code(e) != LV_EVENT_CLICKED) {
        return;
    }
    if (s_op_busy) {
        return;
    }
    if (!app_audio_is_ready()) {
        lv_label_set_text(s_hint, "Mic not ready");
        return;
    }
    if (s_chat_hold_active) {
        return;
    }

    if (!app_audio_recording()) {
        if (voice_cloud_disabled_by_privacy()) {
            lv_label_set_text(s_hint, "Privacy on");
            return;
        }
        s_desk_stream_stop_req = false;
        esp_err_t se = app_http_voice_stream_start(s_desk_stream_sid, sizeof(s_desk_stream_sid));
        if (se != ESP_OK) {
            lv_label_set_text(s_hint, "Stream start fail");
            ESP_LOGW(TAG, "desk stream start %s", esp_err_to_name(se));
            s_desk_stream_sid[0] = '\0';
            return;
        }
        app_audio_record_set(true);
        s_desk_rec_active = true;
        s_rec_timing = true;
        s_rec_start_us = esp_timer_get_time();
        lv_obj_add_state(s_btn_chat, LV_STATE_DISABLED);
        rec_anim_start();
        rec_btn_style_rec();
        if (s_meter == NULL) {
            s_meter = lv_timer_create(meter_cb, 1000, NULL);
        }
        lv_timer_reset(s_meter);
        rec_hint_show_elapsed();
        if (s_desk_stream_timer == NULL) {
            s_desk_stream_timer = lv_timer_create(desk_stream_timer_cb, STREAM_TIMER_MS, NULL);
        }
        lv_timer_reset(s_desk_stream_timer);
        if (s_desk_stream_task) {
            xTaskNotifyGive(s_desk_stream_task);
        }
        ESP_LOGI(TAG, "desk stream sid %s", s_desk_stream_sid);
        return;
    }

    if (!s_desk_rec_active) {
        return;
    }

    if (s_desk_stream_timer) {
        lv_timer_del(s_desk_stream_timer);
        s_desk_stream_timer = NULL;
    }
    rec_anim_stop();
    if (s_meter) {
        lv_timer_del(s_meter);
        s_meter = NULL;
    }
    s_rec_timing = false;
    app_audio_record_set(false);
    rec_btn_style_idle();
    lv_label_set_text(s_hint, "Sealing…");
    s_op_busy = true;
    lv_obj_add_state(s_btn_rec, LV_STATE_DISABLED);
    lv_obj_add_state(s_btn_chat, LV_STATE_DISABLED);
    s_desk_stream_stop_req = true;
    if (s_desk_stream_task) {
        xTaskNotifyGive(s_desk_stream_task);
    }
}

static void style_panel_btn(lv_obj_t *btn)
{
    lv_obj_remove_style_all(btn);
    lv_obj_add_flag(btn, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_clear_flag(btn, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_radius(btn, 14, LV_PART_MAIN);
    lv_obj_set_style_shadow_width(btn, 6, LV_PART_MAIN);
    lv_obj_set_style_shadow_ofs_y(btn, 2, LV_PART_MAIN);
    lv_obj_set_style_shadow_opa(btn, LV_OPA_20, LV_PART_MAIN);
}

void lv_ai_chat_init(void)
{
    s_op_busy = false;
    s_chat_hold_active = false;
    s_desk_rec_active = false;
    s_desk_stream_sid[0] = '\0';
    s_desk_stream_stop_req = false;
    s_desk_stream_task = NULL;
    s_desk_stream_timer = NULL;
    s_meter = NULL;
    s_rec_anim = NULL;
    s_clock_timer = NULL;
    s_rec_start_us = 0;
    s_rec_timing = false;

#if !CONFIG_LV_FONT_SIMSUN_16_CJK
    ESP_LOGW(TAG, "SIMSUN CJK off");
#else
    ESP_LOGI(TAG, "Chat log: Simsun subset");
#endif

    lv_obj_t *scr = lv_scr_act();
    lv_obj_set_style_bg_color(scr, lv_color_hex(COLOR_SCR_BG), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(scr, LV_OPA_COVER, LV_PART_MAIN);
    apply_font_ui(scr);

    const lv_coord_t hdr_h = 46;
    const int hint_h = 28;
    const int bottom_gap = 6;
    const int btn_row_h = BTN_ROW_H;
    const int top_below_hdr = hdr_h + 4;
    int log_h = lcddev.height - top_below_hdr - btn_row_h - hint_h - bottom_gap - 20;
    if (log_h < 64) {
        log_h = 64;
    }

    s_header = lv_obj_create(scr);
    lv_obj_set_size(s_header, lcddev.width, hdr_h);
    lv_obj_align(s_header, LV_ALIGN_TOP_MID, 0, 0);
    lv_obj_set_style_bg_color(s_header, lv_color_hex(COLOR_HEADER_BG), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(s_header, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_border_width(s_header, 0, LV_PART_MAIN);
    lv_obj_set_style_pad_left(s_header, 10, LV_PART_MAIN);
    lv_obj_set_style_pad_right(s_header, 10, LV_PART_MAIN);
    lv_obj_set_style_pad_top(s_header, 4, LV_PART_MAIN);
    lv_obj_set_style_pad_bottom(s_header, 4, LV_PART_MAIN);
    lv_obj_clear_flag(s_header, LV_OBJ_FLAG_SCROLLABLE);

    s_title = lv_label_create(s_header);
    style_ui_text(s_title);
    lv_label_set_text(s_title, "Ai Watch");
    lv_obj_align(s_title, LV_ALIGN_TOP_LEFT, 0, 0);

    s_clock = lv_label_create(s_header);
    style_ui_text(s_clock);
    lv_obj_set_style_text_color(s_clock, lv_color_hex(COLOR_TEXT), LV_PART_MAIN);
    lv_label_set_text(s_clock, "--:--");
    lv_obj_align(s_clock, LV_ALIGN_TOP_RIGHT, -10, 0);

    s_title_glyph = lv_label_create(s_header);
    apply_font_symbols(s_title_glyph);
    lv_obj_set_style_text_color(s_title_glyph, lv_color_hex(COLOR_ACCENT), LV_PART_MAIN);
    lv_label_set_text(s_title_glyph, LV_SYMBOL_AUDIO);
    lv_obj_align_to(s_title_glyph, s_title, LV_ALIGN_OUT_RIGHT_MID, 6, 0);

    s_wifi_glyph = lv_label_create(s_header);
    apply_font_symbols(s_wifi_glyph);
    lv_obj_set_style_text_color(s_wifi_glyph, lv_color_hex(COLOR_TEXT_MUTE), LV_PART_MAIN);
    lv_label_set_text(s_wifi_glyph, LV_SYMBOL_WIFI);
    lv_obj_align(s_wifi_glyph, LV_ALIGN_BOTTOM_LEFT, 0, 0);

    s_status = lv_label_create(s_header);
    style_ui_text(s_status);
    lv_obj_set_style_text_color(s_status, lv_color_hex(COLOR_TEXT_MUTE), LV_PART_MAIN);
    lv_label_set_long_mode(s_status, LV_LABEL_LONG_SCROLL_CIRCULAR);
    lv_obj_set_width(s_status, lcddev.width - 50);
    {
        char buf[120];
        snprintf(buf, sizeof(buf), "%s  %s", app_wifi_ip_str(), app_http_base_url());
        lv_label_set_text(s_status, buf);
    }
    lv_obj_align_to(s_status, s_wifi_glyph, LV_ALIGN_OUT_RIGHT_MID, 6, 0);

    s_log_scroll = lv_obj_create(scr);
    lv_obj_set_size(s_log_scroll, lcddev.width - 14, log_h);
    lv_obj_align(s_log_scroll, LV_ALIGN_TOP_MID, 0, top_below_hdr);
    lv_obj_set_style_bg_color(s_log_scroll, lv_color_hex(COLOR_CARD), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(s_log_scroll, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_border_color(s_log_scroll, lv_color_hex(COLOR_BORDER), LV_PART_MAIN);
    lv_obj_set_style_border_width(s_log_scroll, 2, LV_PART_MAIN);
    lv_obj_set_style_radius(s_log_scroll, 12, LV_PART_MAIN);
    lv_obj_set_style_pad_all(s_log_scroll, 8, LV_PART_MAIN);
    lv_obj_set_scroll_dir(s_log_scroll, LV_DIR_VER);

    s_log_label = lv_label_create(s_log_scroll);
    lv_label_set_long_mode(s_log_label, LV_LABEL_LONG_WRAP);
    lv_obj_set_width(s_log_label, lcddev.width - 38);
    style_chat_text(s_log_label);
    s_log[0] = '\0';
    snprintf(s_log, sizeof(s_log),
             "[Tip] Left: hold AI, release to send. Right: desk = PCM stream to PC, stop→WAV on /desk.\n\n");
    lv_label_set_text(s_log_label, s_log);

    s_hint_row = lv_obj_create(scr);
    lv_obj_set_size(s_hint_row, lcddev.width - 16, hint_h);
    lv_obj_align(s_hint_row, LV_ALIGN_BOTTOM_MID, 0, -btn_row_h - bottom_gap - 6);
    lv_obj_set_style_bg_opa(s_hint_row, LV_OPA_TRANSP, LV_PART_MAIN);
    lv_obj_set_style_border_width(s_hint_row, 0, LV_PART_MAIN);
    lv_obj_clear_flag(s_hint_row, LV_OBJ_FLAG_SCROLLABLE);

#if LV_USE_SPINNER
    s_spinner = lv_spinner_create(s_hint_row, 900, 60);
    lv_obj_set_size(s_spinner, 22, 22);
    lv_obj_align(s_spinner, LV_ALIGN_LEFT_MID, 2, 0);
    lv_obj_set_style_arc_color(s_spinner, lv_color_hex(COLOR_ACCENT), LV_PART_INDICATOR);
    lv_obj_set_style_arc_width(s_spinner, 3, LV_PART_INDICATOR);
    lv_obj_add_flag(s_spinner, LV_OBJ_FLAG_HIDDEN);
#endif

    s_hint = lv_label_create(s_hint_row);
    style_ui_text(s_hint);
    lv_obj_set_style_text_color(s_hint, lv_color_hex(COLOR_TEXT_MUTE), LV_PART_MAIN);
    lv_obj_set_width(s_hint, lcddev.width - 52);
    lv_label_set_long_mode(s_hint, LV_LABEL_LONG_SCROLL_CIRCULAR);
    lv_label_set_text(s_hint, k_hint_idle);
    lv_obj_align(s_hint, LV_ALIGN_LEFT_MID, 4, 0);

    const int btn_w = (lcddev.width - BTN_PAD * 3) / 2;
    s_btn_chat = lv_obj_create(scr);
    style_panel_btn(s_btn_chat);
    lv_obj_set_size(s_btn_chat, btn_w, btn_row_h);
    lv_obj_align(s_btn_chat, LV_ALIGN_BOTTOM_LEFT, BTN_PAD, -bottom_gap);
    chat_btn_style_idle();
    lv_obj_add_event_cb(s_btn_chat, chat_btn_event, LV_EVENT_LONG_PRESSED, NULL);
    lv_obj_add_event_cb(s_btn_chat, chat_btn_event, LV_EVENT_RELEASED, NULL);

    s_lbl_chat = lv_label_create(s_btn_chat);
    apply_font_ui(s_lbl_chat);
    lv_obj_set_style_text_color(s_lbl_chat, lv_color_hex(0xFFFFFF), LV_PART_MAIN);
    lv_label_set_text(s_lbl_chat, LV_SYMBOL_LOOP " Hold AI");
    lv_obj_center(s_lbl_chat);
    lv_obj_add_flag(s_lbl_chat, LV_OBJ_FLAG_EVENT_BUBBLE);

    s_btn_rec = lv_obj_create(scr);
    style_panel_btn(s_btn_rec);
    lv_obj_set_size(s_btn_rec, btn_w, btn_row_h);
    lv_obj_align(s_btn_rec, LV_ALIGN_BOTTOM_RIGHT, -BTN_PAD, -bottom_gap);
    rec_btn_style_idle();
    lv_obj_add_event_cb(s_btn_rec, record_btn_event, LV_EVENT_CLICKED, NULL);

    s_lbl_rec = lv_label_create(s_btn_rec);
    apply_font_ui(s_lbl_rec);
    lv_obj_set_style_text_color(s_lbl_rec, lv_color_hex(0xFFFFFF), LV_PART_MAIN);
    lv_label_set_text(s_lbl_rec, LV_SYMBOL_AUDIO " Desk");
    lv_obj_center(s_lbl_rec);
    lv_obj_add_flag(s_lbl_rec, LV_OBJ_FLAG_EVENT_BUBBLE);

    BaseType_t t =
        xTaskCreatePinnedToCore(desk_stream_upload_task, "desk_stream", 24576, NULL, 5, &s_desk_stream_task, 1);
    if (t != pdPASS) {
        t = xTaskCreate(desk_stream_upload_task, "desk_stream", 24576, NULL, 5, &s_desk_stream_task);
    }
    if (t != pdPASS) {
        ESP_LOGE(TAG, "desk_stream task create failed");
    }

    app_wifi_set_time_sync_cb(app_wifi_time_bridge, NULL);
    wall_clock_refresh();
    s_clock_timer = lv_timer_create(clock_timer_cb, 2000, NULL);
}
