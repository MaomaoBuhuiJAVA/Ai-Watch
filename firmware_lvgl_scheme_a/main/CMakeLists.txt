# 方案 A：教程 main（含 APP） + Ai Watch 网络与音频源文件
# 勿同时写 SRC_DIRS 与 SRCS（ESP-IDF 会忽略 SRC_DIRS）
idf_component_register(
    SRC_DIRS
        "."
        "APP"
    INCLUDE_DIRS
        "."
        "APP"
    PRIV_REQUIRES
        nvs_flash
        esp_wifi
        esp_netif
        esp_event
        esp_http_client
        json
        driver
        esp_timer
        spi_flash
        heap
        freertos
        esp_codec_dev
        BSP
        LVGL
)
