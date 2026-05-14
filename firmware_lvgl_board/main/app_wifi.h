#pragma once

#include "esp_err.h"
#include <stdbool.h>

/** netif + esp_wifi_init + 事件注册（只调用一次） */
esp_err_t app_wifi_system_init(void);
/** 设置 STA 凭据并启动（STA 模式） */
esp_err_t app_wifi_sta_set_and_start(const char *ssid, const char *pass);
esp_err_t app_wifi_stop(void);
/** 仅 AP：手机连热点后在浏览器打开 http://192.168.4.1 配网 */
esp_err_t app_wifi_ap_start_provisioning(void);
/** timeout_ms==0 表示一直等待 */
esp_err_t app_wifi_wait_connected(uint32_t timeout_ms);
const char *app_wifi_ip_str(void);

typedef void (*app_wifi_time_sync_cb_t)(void *user);
void app_wifi_set_time_sync_cb(app_wifi_time_sync_cb_t cb, void *user);
bool app_wifi_time_synced(void);
