#include "ha_client.h"

#include <stdio.h>
#include <string.h>

#include "esp_http_client.h"
#include "esp_log.h"

#include "led.h"

static const char *TAG = "ha_client";

bool ha_client_send_webhook(const char *url, const char *content_id,
                            const char *content_type) {
  if (!url || !content_id) {
    return false;
  }

  // Build POST body
  char post_data[256];
  snprintf(post_data, sizeof(post_data),
           "content_id=%s&entity_id=%s&content_type=%s", content_id,
           CONFIG_SONOS_ENTITY_ID, content_type);

  esp_http_client_config_t config = {
      .url = url,
      .method = HTTP_METHOD_POST,
  };

  esp_http_client_handle_t client = esp_http_client_init(&config);
  if (!client) {
    ESP_LOGE(TAG, "Failed to init HTTP client");
    return false;
  }

  esp_http_client_set_header(client, "Content-Type",
                             "application/x-www-form-urlencoded");
  esp_http_client_set_post_field(client, post_data, strlen(post_data));

  esp_err_t err = esp_http_client_perform(client);
  bool ok = false;
  if (err == ESP_OK) {
    ESP_LOGI(TAG, "POST Status = %d, content_length = %d",
             esp_http_client_get_status_code(client),
             esp_http_client_get_content_length(client));
    led_set_blink_mode(LED_BLINK_OFF); // successful request
    ok = true;
  } else {
    ESP_LOGE(TAG, "HTTP POST request failed: %s", esp_err_to_name(err));
    led_set_blink_mode(LED_BLINK_FAST); // indicate error
  }

  esp_http_client_cleanup(client);
  return ok;
}