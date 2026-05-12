#pragma once

#include "esp_err.h"
#include <stdbool.h>

esp_err_t app_wifi_init_sta(void);
/** timeout_ms==0 → wait forever; otherwise max wait in ms. */
esp_err_t app_wifi_wait_connected(uint32_t timeout_ms);
const char *app_wifi_ip_str(void);

/** Called when时间已同步（HTTP /api/time 或 SNTP），保持简短；UI 可用 lv_async_call。 */
typedef void (*app_wifi_time_sync_cb_t)(void *user);
void app_wifi_set_time_sync_cb(app_wifi_time_sync_cb_t cb, void *user);
bool app_wifi_time_synced(void);
