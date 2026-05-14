#pragma once

#include "esp_err.h"
#include <stdbool.h>
#include <stddef.h>

#define CRED_SSID_MAX 33
#define CRED_PASS_MAX 65
#define CRED_URL_MAX  192

/**
 * 从 NVS 读取 wifi_ssid / wifi_pass / srv_url。
 * @return true 表示 NVS 里已有非空 wifi_ssid（优先用 NVS，不再用 menuconfig 的 WiFi）。
 * @return false 表示 NVS 无 WiFi：会用 menuconfig 的 CONFIG_AIW_WIFI_* 作为一次尝试。
 * srv_url：若 NVS 有 srv_url 则写入 url，否则 url 填 CONFIG_AIW_SERVER_BASE_URL。
 */
bool cred_load_all(char *ssid, size_t ssid_sz, char *pass, size_t pass_sz, char *url, size_t url_sz);

/** 配网页提交后写入 NVS，随后应 esp_restart() */
esp_err_t cred_save_wifi_and_url(const char *ssid, const char *pass, const char *url);
