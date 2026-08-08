#include <stdio.h>

#include "esp_err.h"
#include "esp_event.h"
#include "esp_log.h"
#include "esp_netif.h"
#include "freertos/FreeRTOS.h"

#include "nvs_flash.h"

#include "dispatch.h"
#include "led.h"
#include "rc522_scanner.h"
#include "wifi.h"

static const char *TAG = "main";

void app_main(void) {
  led_init();
  led_set(true); // LED ON while booting

  ESP_ERROR_CHECK(nvs_flash_init());
  ESP_ERROR_CHECK(esp_netif_init());
  ESP_ERROR_CHECK(esp_event_loop_create_default());

  wifi_init(); // blocks until connected/failed/timeout (up to 10 s)

  char *json = dispatch_load();
  if (!json) {
    ESP_LOGE(TAG, "Failed to read JSON file");
    return;
  }

  led_start_blink_task();

  esp_err_t ret = rc522_scanner_init(json);
  if (ret != ESP_OK) {
    ESP_LOGE(TAG, "Failed to start RC522 scanner: %s", esp_err_to_name(ret));
  }

  led_set(false); // LED OFF (blink task takes over if needed)
}