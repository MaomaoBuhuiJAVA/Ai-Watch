#pragma once

#include "esp_err.h"

/** 创建默认 STA+AP netif、注册事件、esp_wifi_init（只调用一次） */
esp_err_t app_wifi_system_init(void);
/** 设置 STA 凭据并启动 WiFi（STA 模式） */
esp_err_t app_wifi_sta_set_and_start(const char *ssid, const char *pass);
esp_err_t app_wifi_stop(void);
/** 停止后调用：仅 AP 模式，用于手机连热点配网 */
esp_err_t app_wifi_ap_start_provisioning(void);
/** timeout_ms==0 → 等到连上为止 */
esp_err_t app_wifi_wait_connected(uint32_t timeout_ms);
const char *app_wifi_ip_str(void);
