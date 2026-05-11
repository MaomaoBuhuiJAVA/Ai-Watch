#include "app_console.h"
#include "app_wifi.h"
#include "app_http.h"
#include "app_audio_codec.h"
#include "xl9555.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "cJSON.h"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

static void trim_newline(char *s)
{
    size_t n = strlen(s);
    while (n > 0 && (s[n - 1] == '\n' || s[n - 1] == '\r')) {
        s[--n] = '\0';
    }
}

static void chat_task(void *arg)
{
    (void)arg;
    vTaskDelay(pdMS_TO_TICKS(1500));
    printf("\r\n\r\n=== Ai Watch (no screen) ===\r\n");
    printf("Server: %s\r\n", app_http_base_url());
    printf("Type a line + Enter -> DeepSeek chat. Empty line ignored.\r\n");
    printf("KEY1 on board -> toggle recording (stop -> upload WAV if privacy off).\r\n\r\n");

    char line[384];
    for (;;) {
        printf("> ");
        fflush(stdout);
        if (fgets(line, sizeof(line), stdin) == NULL) {
            vTaskDelay(pdMS_TO_TICKS(200));
            continue;
        }
        trim_newline(line);
        if (line[0] == '\0') {
            continue;
        }

        cJSON *root = cJSON_CreateObject();
        if (!root) {
            continue;
        }
        cJSON_AddStringToObject(root, "message", line);
        char *body = cJSON_PrintUnformatted(root);
        cJSON_Delete(root);
        if (!body) {
            continue;
        }

        char resp[1536];
        esp_err_t err = app_http_post_json("/api/chat", body, resp, sizeof(resp));
        free(body);

        if (err == ESP_OK) {
            printf("%s\r\n", resp);
        } else {
            printf("(chat failed, check WiFi and server)\r\n");
        }
    }
}

static void key_task(void *arg)
{
    (void)arg;
    for (;;) {
        uint8_t k = xl9555_key_scan(0);
        if (k == KEY1_PRES) {
            app_audio_record_toggle();
            printf("\r\n[KEY1] recording %s\r\n", app_audio_recording() ? "ON" : "OFF");
        }
        vTaskDelay(pdMS_TO_TICKS(80));
    }
}

void app_console_start(void)
{
    xTaskCreatePinnedToCore(key_task, "keys", 2048, NULL, 5, NULL, 1);
    xTaskCreatePinnedToCore(chat_task, "chat", 8192, NULL, 3, NULL, 0);
}
