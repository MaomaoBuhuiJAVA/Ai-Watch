#include "app_ui_lvgl.h"
#include "board_lcd.h"
#include "touch.h"
#include "xl9555.h"
#include "app_wifi.h"
#include "app_http.h"
#include "app_audio_codec.h"
#include "sdkconfig.h"
#include "nvs_flash.h"
#include "nvs.h"
#include "esp_timer.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "lvgl.h"
#include "cJSON.h"
#include <stdlib.h>
#include <string.h>

static const char *TAG = "ui";

static lv_obj_t *lbl_ip;
static lv_obj_t *lbl_rec_state;
static lv_obj_t *sw_privacy;
static lv_obj_t *ta_chat;

static bool s_privacy = false;

static void load_privacy(void)
{
    nvs_handle_t h;
    if (nvs_open("aiw", NVS_READONLY, &h) != ESP_OK) {
        return;
    }
    uint8_t v = 0;
    if (nvs_get_u8(h, "privacy", &v) == ESP_OK) {
        s_privacy = v != 0;
    }
    nvs_close(h);
}

static void save_privacy(bool on)
{
    nvs_handle_t h;
    if (nvs_open("aiw", NVS_READWRITE, &h) != ESP_OK) {
        return;
    }
    nvs_set_u8(h, "privacy", on ? 1 : 0);
    nvs_commit(h);
    nvs_close(h);
}

static void on_sw_privacy(lv_event_t *e)
{
    lv_obj_t *sw = lv_event_get_target(e);
    s_privacy = lv_obj_has_state(sw, LV_STATE_CHECKED);
    save_privacy(s_privacy);
}

static void on_btn_rec(lv_event_t *e)
{
    (void)e;
    app_audio_record_toggle();
}

static void on_btn_send(lv_event_t *e)
{
    (void)e;
    const char *txt = lv_textarea_get_text(ta_chat);
    if (!txt || strlen(txt) == 0) {
        return;
    }
    cJSON *root = cJSON_CreateObject();
    if (!root) {
        return;
    }
    cJSON_AddStringToObject(root, "message", txt);
    char *printed = cJSON_PrintUnformatted(root);
    cJSON_Delete(root);
    if (!printed) {
        return;
    }
    char resp[1024];
    if (app_http_post_json("/api/chat", printed, resp, sizeof(resp)) == ESP_OK) {
        lv_textarea_set_text(ta_chat, resp);
    } else {
        lv_textarea_set_text(ta_chat, "(请求失败)");
    }
    free(printed);
}

static void on_btn_sync_plan(lv_event_t *e)
{
    (void)e;
    lv_obj_t *mbox = lv_msgbox_create(NULL, "日程", "已从服务端同步（占位）", NULL, true);
    lv_obj_center(mbox);
}

static void lvgl_flush_cb(lv_disp_drv_t *drv, const lv_area_t *area, lv_color_t *color_map)
{
    esp_lcd_panel_handle_t panel = (esp_lcd_panel_handle_t)drv->user_data;
    int x1 = area->x1;
    int y1 = area->y1;
    int x2 = area->x2 + 1;
    int y2 = area->y2 + 1;
    esp_lcd_panel_draw_bitmap(panel, x1, y1, x2, y2, color_map);
    lv_disp_flush_ready(drv);
}

static void lvgl_tick_cb(void *arg)
{
    (void)arg;
    lv_tick_inc(1);
}

static void touchpad_read(lv_indev_drv_t *indev_drv, lv_indev_data_t *data)
{
    (void)indev_drv;
    static lv_coord_t lx = 0, ly = 0;
    if (tp_dev.scan) {
        tp_dev.scan();
    }
    if (tp_dev.sta & TP_PRES_DOWN) {
        data->state = LV_INDEV_STATE_PR;
        lx = (lv_coord_t)tp_dev.y[0];
        ly = (lv_coord_t)(lcddev.height - tp_dev.x[0]);
    } else {
        data->state = LV_INDEV_STATE_REL;
    }
    data->point.x = lx;
    data->point.y = ly;
}

static void ui_build(void)
{
    lv_obj_t *tv = lv_tabview_create(lv_scr_act(), LV_DIR_TOP, 36);
    lv_obj_t *tab_home = lv_tabview_add_tab(tv, "首页");
    lv_obj_t *tab_rec = lv_tabview_add_tab(tv, "录音");
    lv_obj_t *tab_plan = lv_tabview_add_tab(tv, "日程");
    lv_obj_t *tab_rev = lv_tabview_add_tab(tv, "复盘");
    lv_obj_t *tab_set = lv_tabview_add_tab(tv, "设置");

    lbl_ip = lv_label_create(tab_home);
    lv_label_set_text_fmt(lbl_ip, "设备: %s\nIP: %s\n服务器: %s", CONFIG_AIW_DEVICE_NAME, app_wifi_ip_str(),
                          app_http_base_url());
    lv_obj_align(lbl_ip, LV_ALIGN_TOP_LEFT, 0, 0);

    lv_obj_t *lbl_h2 = lv_label_create(tab_home);
    lv_label_set_text(lbl_h2, "按键 KEY1：一键启停板载麦克风录音\n触摸各页切换功能");
    lv_obj_align(lbl_h2, LV_ALIGN_TOP_LEFT, 0, 80);

    lbl_rec_state = lv_label_create(tab_rec);
    lv_label_set_text(lbl_rec_state, "录音：停止");
    lv_obj_align(lbl_rec_state, LV_ALIGN_TOP_LEFT, 0, 0);
    lv_obj_t *btn = lv_btn_create(tab_rec);
    lv_obj_set_size(btn, 120, 44);
    lv_obj_align(btn, LV_ALIGN_TOP_LEFT, 0, 40);
    lv_obj_add_event_cb(btn, on_btn_rec, LV_EVENT_CLICKED, NULL);
    lv_obj_t *bl = lv_label_create(btn);
    lv_label_set_text(bl, "启停");
    lv_obj_center(bl);

    lv_obj_t *lp = lv_label_create(tab_plan);
    lv_label_set_text(lp, "工作计划：服务端维护 SQLite，\n支持提醒字段扩展。");
    lv_obj_align(lp, LV_ALIGN_TOP_LEFT, 0, 0);
    lv_obj_t *bs = lv_btn_create(tab_plan);
    lv_obj_set_size(bs, 140, 40);
    lv_obj_align(bs, LV_ALIGN_TOP_LEFT, 0, 70);
    lv_obj_add_event_cb(bs, on_btn_sync_plan, LV_EVENT_CLICKED, NULL);
    lv_obj_t *lbs = lv_label_create(bs);
    lv_label_set_text(lbs, "同步日程");
    lv_obj_center(lbs);

    lv_obj_t *lr = lv_label_create(tab_rev);
    lv_label_set_text(lr, "语音复盘：上传后由服务端转写与检索。\n（此处接列表/播放器控件）");
    lv_obj_align(lr, LV_ALIGN_TOP_LEFT, 0, 0);

    lv_obj_t *ls = lv_label_create(tab_set);
    lv_label_set_text(ls, "隐私：开启后不向服务器上传新录音。");
    lv_obj_align(ls, LV_ALIGN_TOP_LEFT, 0, 0);
    sw_privacy = lv_switch_create(tab_set);
    lv_obj_align(sw_privacy, LV_ALIGN_TOP_LEFT, 0, 36);
    if (s_privacy) {
        lv_obj_add_state(sw_privacy, LV_STATE_CHECKED);
    }
    lv_obj_add_event_cb(sw_privacy, on_sw_privacy, LV_EVENT_VALUE_CHANGED, NULL);

    ta_chat = lv_textarea_create(tab_set);
    lv_obj_set_size(ta_chat, 280, 72);
    lv_obj_align(ta_chat, LV_ALIGN_TOP_LEFT, 0, 90);
    lv_textarea_set_placeholder_text(ta_chat, "输入问题，点发送调 DeepSeek");
    lv_obj_t *bsend = lv_btn_create(tab_set);
    lv_obj_set_size(bsend, 80, 36);
    lv_obj_align(bsend, LV_ALIGN_TOP_LEFT, 0, 170);
    lv_obj_add_event_cb(bsend, on_btn_send, LV_EVENT_CLICKED, NULL);
    lv_obj_t *lbd = lv_label_create(bsend);
    lv_label_set_text(lbd, "发送");
    lv_obj_center(lbd);
}

static void ui_timer_cb(lv_timer_t *t)
{
    (void)t;
    uint8_t k = xl9555_key_scan(0);
    if (k == KEY1_PRES) {
        app_audio_record_toggle();
    }
    if (app_audio_recording()) {
        lv_label_set_text_fmt(lbl_rec_state, "录音：进行中… %u KB", (unsigned)(app_audio_pcm_bytes() / 1024));
    } else {
        lv_label_set_text(lbl_rec_state, "录音：停止");
    }
}

static void lvgl_port_disp_init(void)
{
    static lv_disp_draw_buf_t disp_buf;
    static lv_disp_drv_t disp_drv;
    const uint32_t buf_rows = 40;
    void *buf1 = heap_caps_malloc(lcddev.width * buf_rows * sizeof(lv_color_t), MALLOC_CAP_DMA);
    void *buf2 = heap_caps_malloc(lcddev.width * buf_rows * sizeof(lv_color_t), MALLOC_CAP_DMA);
    if (!buf1 || !buf2) {
        ESP_LOGE(TAG, "lvgl draw buffer alloc failed");
        return;
    }

    lv_disp_draw_buf_init(&disp_buf, buf1, buf2, lcddev.width * buf_rows);
    lv_disp_drv_init(&disp_drv);
    disp_drv.hor_res = (lv_coord_t)lcddev.width;
    disp_drv.ver_res = (lv_coord_t)lcddev.height;
    disp_drv.flush_cb = lvgl_flush_cb;
    disp_drv.draw_buf = &disp_buf;
    disp_drv.user_data = lcddev.lcd_panel_handle;
    lv_disp_drv_register(&disp_drv);
}

static void lvgl_port_indev_init(void)
{
    static lv_indev_drv_t indev_drv;
    lv_indev_drv_init(&indev_drv);
    indev_drv.type = LV_INDEV_TYPE_POINTER;
    indev_drv.read_cb = touchpad_read;
    lv_indev_drv_register(&indev_drv);
}

static void lv_demo_task(void *arg)
{
    (void)arg;
    load_privacy();
    lv_init();
    lvgl_port_disp_init();
    lvgl_port_indev_init();

    const esp_timer_create_args_t tick_args = {.callback = &lvgl_tick_cb, .name = "lvgl_tick"};
    esp_timer_handle_t tick = NULL;
    ESP_ERROR_CHECK(esp_timer_create(&tick_args, &tick));
    ESP_ERROR_CHECK(esp_timer_start_periodic(tick, 1000));

    ui_build();
    lv_timer_create(ui_timer_cb, 80, NULL);

    while (1) {
        lv_timer_handler();
        vTaskDelay(pdMS_TO_TICKS(10));
    }
}

void app_ui_lvgl_start(void)
{
    xTaskCreatePinnedToCore(lv_demo_task, "lv_ui", 1024 * 12, NULL, 3, NULL, 0);
}
