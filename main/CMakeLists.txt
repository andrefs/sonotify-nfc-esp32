idf_component_register(
    SRCS "sonotify-nfc-esp32.c"
    INCLUDE_DIRS "."
    REQUIRES
        spiffs
        cjson
        esp_http_client 
        esp_netif
        esp_event
        nvs_flash
    PRIV_REQUIRES
        mbedtls
        esp_wifi
)
