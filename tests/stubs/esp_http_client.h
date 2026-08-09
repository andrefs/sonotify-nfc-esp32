#ifndef ESP_HTTP_CLIENT_STUB_H
#define ESP_HTTP_CLIENT_STUB_H

#include <stddef.h>

#include "esp_err.h"

typedef struct esp_http_client *esp_http_client_handle_t;
typedef int esp_http_client_method_t;

#define HTTP_METHOD_GET 0

typedef int esp_http_client_event_id_t;
#define HTTP_EVENT_ON_DATA 3

typedef struct {
  int event_id;
  void *data;
  int data_len;
  void *user_data;
} esp_http_client_event_t;

typedef esp_err_t (*esp_http_client_event_handle_t)(esp_http_client_event_t *);
typedef void (*http_crt_bundle_attach_t)(void);

typedef struct {
  const char *url;
  esp_http_client_method_t method;
  int timeout_ms;
  void *user_data;
  esp_http_client_event_handle_t event_handler;
  void (*crt_bundle_attach)(void);
} esp_http_client_config_t;

esp_http_client_handle_t esp_http_client_init(const esp_http_client_config_t *);
esp_err_t esp_http_client_perform(esp_http_client_handle_t);
esp_err_t esp_http_client_cleanup(esp_http_client_handle_t);
int esp_http_client_get_status_code(esp_http_client_handle_t);

#endif