#include "app_wifi.h"
#include "sdkconfig.h"
#include "esp_wifi.h"
#include "esp_event.h"
#include "esp_netif.h"
#include "esp_log.h"
#include "esp_check.h"
#include "esp_mac.h"
#include "freertos/FreeRTOS.h"
#include "freertos/event_groups.h"
#include <string.h>

static const char *TAG = "app_wifi";

#define WIFI_CONNECTED_BIT BIT0

static EventGroupHandle_t s_wifi_evt;
static char s_ip[20] = "0.0.0.0";

static void on_wifi_event(void *arg, esp_event_base_t base, int32_t id, void *data)
{
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
    if (base == IP_EVENT && id == IP_EVENT_STA_GOT_IP) {
        ip_event_got_ip_t *ev = (ip_event_got_ip_t *)data;
        snprintf(s_ip, sizeof(s_ip), IPSTR, IP2STR(&ev->ip_info.ip));
        ESP_LOGI(TAG, "got ip: %s", s_ip);
        xEventGroupSetBits(s_wifi_evt, WIFI_CONNECTED_BIT);
    }
}

esp_err_t app_wifi_system_init(void)
{
    s_wifi_evt = xEventGroupCreate();
    ESP_RETURN_ON_FALSE(s_wifi_evt, ESP_ERR_NO_MEM, TAG, "evt group");

    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());
    esp_netif_create_default_wifi_sta();
    esp_netif_create_default_wifi_ap();

    wifi_init_config_t icfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&icfg));

    ESP_ERROR_CHECK(esp_event_handler_instance_register(WIFI_EVENT, ESP_EVENT_ANY_ID, &on_wifi_event, NULL, NULL));
    ESP_ERROR_CHECK(esp_event_handler_instance_register(IP_EVENT, IP_EVENT_STA_GOT_IP, &on_ip_event, NULL, NULL));
    return ESP_OK;
}

esp_err_t app_wifi_sta_set_and_start(const char *ssid, const char *pass)
{
    wifi_config_t wcfg = {0};
    if (!ssid || !ssid[0]) {
        return ESP_ERR_INVALID_ARG;
    }
    strncpy((char *)wcfg.sta.ssid, ssid, sizeof(wcfg.sta.ssid) - 1);
    strncpy((char *)wcfg.sta.password, pass ? pass : "", sizeof(wcfg.sta.password) - 1);
    wcfg.sta.threshold.authmode = WIFI_AUTH_WPA2_PSK;

    ESP_LOGI(TAG, "STA connecting SSID=\"%s\"", ssid);

    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &wcfg));
    xEventGroupClearBits(s_wifi_evt, WIFI_CONNECTED_BIT);
    ESP_ERROR_CHECK(esp_wifi_start());
    return ESP_OK;
}

esp_err_t app_wifi_stop(void)
{
    return esp_wifi_stop();
}

esp_err_t app_wifi_ap_start_provisioning(void)
{
    uint8_t mac[6];
    esp_read_mac(mac, ESP_MAC_WIFI_SOFTAP);

    wifi_config_t wc = {0};
    snprintf((char *)wc.ap.ssid, sizeof(wc.ap.ssid), "AiWatch-%02X%02X", mac[4], mac[5]);
    wc.ap.ssid_len = (uint8_t)strlen((char *)wc.ap.ssid);
    strncpy((char *)wc.ap.password, CONFIG_AIW_PROV_AP_PASSWORD, sizeof(wc.ap.password) - 1);
    wc.ap.max_connection = 4;
    wc.ap.authmode = WIFI_AUTH_WPA2_PSK;
    wc.ap.channel = 1;

    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_AP));
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_AP, &wc));
    ESP_ERROR_CHECK(esp_wifi_start());
    ESP_LOGI(TAG, "Provisioning SoftAP: SSID=\"%s\" password=\"%s\" open http://192.168.4.1",
             (char *)wc.ap.ssid, CONFIG_AIW_PROV_AP_PASSWORD);
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
