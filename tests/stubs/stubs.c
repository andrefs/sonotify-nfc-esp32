#include "esp_err.h"
#include "esp_http_client.h"
#include "esp_spiffs.h"

const char *esp_err_to_name(esp_err_t code) {
  switch (code) {
  case ESP_OK:
    return "ESP_OK";
  case ESP_FAIL:
    return "ESP_FAIL";
  case ESP_ERR_NOT_FOUND:
    return "ESP_ERR_NOT_FOUND";
  case ESP_ERR_INVALID_ARG:
    return "ESP_ERR_INVALID_ARG";
  default:
    return "UNKNOWN";
  }
}

esp_err_t esp_vfs_spiffs_register(const esp_vfs_spiffs_conf_t *conf) {
  (void)conf;
  return ESP_OK;
}

esp_err_t esp_spiffs_info(const char *partition_label, size_t *total_bytes,
                          size_t *used_bytes) {
  (void)partition_label;
  if (total_bytes) {
    *total_bytes = 1024 * 1024;
  }
  if (used_bytes) {
    *used_bytes = 0;
  }
  return ESP_OK;
}

static esp_http_client_handle_t s_client_handle = (esp_http_client_handle_t)0x1;

esp_http_client_handle_t esp_http_client_init(const esp_http_client_config_t *config) {
  (void)config;
  return s_client_handle;
}

esp_err_t esp_http_client_perform(esp_http_client_handle_t handle) {
  (void)handle;
  return ESP_FAIL;
}

esp_err_t esp_http_client_cleanup(esp_http_client_handle_t handle) {
  (void)handle;
  return ESP_OK;
}

int esp_http_client_get_status_code(esp_http_client_handle_t handle) {
  (void)handle;
  return 404;
}

void esp_crt_bundle_attach(void) {}