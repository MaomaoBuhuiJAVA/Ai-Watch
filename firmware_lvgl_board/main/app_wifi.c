#include "app_wifi.h"
#include "sdkconfig.h"
#include "app_http.h"
#include "esp_wifi.h"
#include "esp_event.h"
#include "esp_netif.h"
#include "esp_log.h"
#include "esp_check.h"
#include "freertos/FreeRTOS.h"
#include "freertos/event_groups.h"
#include "freertos/task.h"
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <sys/time.h>

#if CONFIG_AIW_SNTP_ENABLE
#include "esp_netif_sntp.h"
#if __has_include("esp_sntp.h")
#include "esp_sntp.h"
#define AIW_HAVE_ESP_SNTP_RESTART 1
#endif
#endif

static const char *TAG = "app_wifi";

#define WIFI_CONNECTED_BIT BIT0

static EventGroupHandle_t s_wifi_evt;
static char s_ip[20] = "0.0.0.0";

/** 任一路径（HTTP /api/time 或 SNTP）校时成功后为 true。 */
static volatile bool s_time_synced;
static app_wifi_time_sync_cb_t s_time_sync_cb;
static void *s_time_sync_user;

static void fire_time_sync_cb(void)
{
    if (s_time_sync_cb) {
        s_time_sync_cb(s_time_sync_user);
    }
}

#if CONFIG_AIW_SNTP_ENABLE
static bool s_sntp_started;

static void sntp_sync_handler(struct timeval *tv)
{
    (void)tv;
    s_time_synced = true;
    fire_time_sync_cb();
}

static void app_wifi_sntp_try_start(void)
{
    if (s_sntp_started) {
        return;
    }
    s_sntp_started = true;
    setenv("TZ", CONFIG_AIW_TZ, 1);
    tzset();

    esp_sntp_config_t cfg = ESP_NETIF_SNTP_DEFAULT_CONFIG(CONFIG_AIW_SNTP_SERVER);
    cfg.sync_cb = sntp_sync_handler;
    cfg.start = true;
    esp_err_t e = esp_netif_sntp_init(&cfg);
    if (e != ESP_OK) {
        ESP_LOGW(TAG, "SNTP init: %s", esp_err_to_name(e));
    } else {
        ESP_LOGI(TAG, "SNTP server=%s TZ=%s", CONFIG_AIW_SNTP_SERVER, CONFIG_AIW_TZ);
    }
}
#endif

static void http_time_sync_task(void *arg)
{
    (void)arg;
    if (app_http_sync_time_from_server() == ESP_OK) {
        s_time_synced = true;
        fire_time_sync_cb();
    }
    vTaskDelete(NULL);
}

void app_wifi_set_time_sync_cb(app_wifi_time_sync_cb_t cb, void *user)
{
    s_time_sync_cb = cb;
    s_time_sync_user = user;
}

bool app_wifi_time_synced(void)
{
    return s_time_synced;
}

static void on_wifi_event(void *arg, esp_event_base_t base, int32_t id, void *data)
{
    (void)arg;
    (void)data;
    if (base == WIFI_EVENT && id == WIFI_EVENT_STA_START) {
        esp_wifi_connect();
    } else if (base == WIFI_EVENT && id == WIFI_EVENT_STA_DISCONNECTED) {
        xEventGroupClearBits(s_wifi_evt, WIFI_CONNECTED_BIT);
        esp_wifi_connect();
        ESP_LOGW(TAG, "STA disconnected, retry…");
    }
}

static void on_ip_event(void *arg, esp_event_base_t base, int32_t id, void *data)
{
    (void)arg;
    (void)base;
    if (id == IP_EVENT_STA_GOT_IP) {
        ip_event_got_ip_t *ev = (ip_event_got_ip_t *)data;
        snprintf(s_ip, sizeof(s_ip), IPSTR, IP2STR(&ev->ip_info.ip));
        ESP_LOGI(TAG, "got ip: %s", s_ip);
        xEventGroupSetBits(s_wifi_evt, WIFI_CONNECTED_BIT);

#if CONFIG_AIW_SNTP_ENABLE
        if (s_sntp_started) {
#if defined(AIW_HAVE_ESP_SNTP_RESTART)
            esp_sntp_restart();
#else
            esp_netif_sntp_deinit();
            s_sntp_started = false;
            app_wifi_sntp_try_start();
#endif
        } else {
            app_wifi_sntp_try_start();
        }
#endif
        if (xTaskCreate(http_time_sync_task, "http_time", 8192, NULL, 4, NULL) != pdPASS) {
            ESP_LOGW(TAG, "http_time task create failed");
        }
    }
}

esp_err_t app_wifi_init_sta(void)
{
    s_wifi_evt = xEventGroupCreate();
    ESP_RETURN_ON_FALSE(s_wifi_evt, ESP_ERR_NO_MEM, TAG, "evt group");

    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());
    esp_netif_create_default_wifi_sta();

    wifi_init_config_t icfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&icfg));

    ESP_ERROR_CHECK(esp_event_handler_instance_register(WIFI_EVENT, ESP_EVENT_ANY_ID, &on_wifi_event, NULL, NULL));
    ESP_ERROR_CHECK(esp_event_handler_instance_register(IP_EVENT, IP_EVENT_STA_GOT_IP, &on_ip_event, NULL, NULL));

    wifi_config_t wcfg = {0};
    strncpy((char *)wcfg.sta.ssid, CONFIG_AIW_WIFI_SSID, sizeof(wcfg.sta.ssid) - 1);
    strncpy((char *)wcfg.sta.password, CONFIG_AIW_WIFI_PASSWORD, sizeof(wcfg.sta.password) - 1);
    wcfg.sta.threshold.authmode = WIFI_AUTH_WPA2_PSK;

    ESP_LOGI(TAG, "Connecting to SSID=\"%s\" (change in menuconfig → Ai Watch if needed)", CONFIG_AIW_WIFI_SSID);

    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &wcfg));
    ESP_ERROR_CHECK(esp_wifi_start());
    return ESP_OK;
}

esp_err_t app_wifi_wait_connected(uint32_t timeout_ms)
{
    TickType_t ticks = (timeout_ms == 0) ? portMAX_DELAY : pdMS_TO_TICKS(timeout_ms);
    EventBits_t bits = xEventGroupWaitBits(s_wifi_evt, WIFI_CONNECTED_BIT, pdFALSE, pdTRUE, ticks);
    if ((bits & WIFI_CONNECTED_BIT) == 0) {
        ESP_LOGE(TAG, "connect timeout");
        return ESP_ERR_TIMEOUT;
    }
    return ESP_OK;
}

const char *app_wifi_ip_str(void)
{
    return s_ip;
}
