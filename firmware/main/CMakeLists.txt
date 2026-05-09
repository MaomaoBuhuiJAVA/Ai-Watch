idf_component_register(
    SRCS
        "app_main.c"
        "app_wifi.c"
        "app_http.c"
        "app_audio_codec.c"
        "app_ui_lvgl.c"
    INCLUDE_DIRS
        "."
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
        hiwonder_board
        lvgl
)

target_compile_definitions(${COMPONENT_LIB} PRIVATE LV_CONF_INCLUDE_SIMPLE=1)
