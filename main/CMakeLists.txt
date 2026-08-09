idf_component_register(
    SRCS "main.c"
         "led.c"
         "wifi.c"
         "dispatch.c"
         "ha_client.c"
         "rc522_scanner.c"
    INCLUDE_DIRS "."
    REQUIRES
        spiffs
        cjson
        esp_http_client
        esp-tls
        esp_netif
        esp_event
        nvs_flash
        driver
    PRIV_REQUIRES
        mbedtls
        esp_wifi
)