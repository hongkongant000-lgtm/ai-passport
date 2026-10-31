idf_component_register(
    SRCS "main.c"
         "app_nav.c"
         "app_radio.c"
         "app_wifi.c"
         "app_ble.c"
         "app_blufi.c"
         "app_blufi_security.c"
         "app_cloud.c"
         "app_power.c"
         "app_ui.c"
         "app_json.c"
         "app_store_names.c"
         "app_store.c"
         "app_xai.c"
         "app_audio_mode.c"
         "app_listen.c"
         "app_speaker.c"
    INCLUDE_DIRS "."
    REQUIRES bsp bt esp_event esp_http_client esp_hw_support esp_netif esp_partition
             esp_timer esp_wifi mbedtls nvs_flash spiffs lwip
)
