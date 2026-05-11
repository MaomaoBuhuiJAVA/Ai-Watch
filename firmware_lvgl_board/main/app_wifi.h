#pragma once

#include "esp_err.h"

esp_err_t app_wifi_init_sta(void);
/** timeout_ms==0 → wait forever; otherwise max wait in ms. */
esp_err_t app_wifi_wait_connected(uint32_t timeout_ms);
const char *app_wifi_ip_str(void);
