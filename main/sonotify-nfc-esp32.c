#include <sdkconfig.h>

#include "freertos/FreeRTOS.h"
#include "freertos/event_groups.h"
#include "freertos/task.h"

#include "esp_crt_bundle.h"
#include "esp_err.h"
#include "esp_event.h"
#include "esp_http_client.h"
#include "esp_log.h"
#include "esp_netif.h"
#include "esp_spiffs.h"
#include "esp_wifi.h"

#include "nvs_flash.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "cJSON.h"

#define WIFI_SSID CONFIG_WIFI_SSID
#define WIFI_PASS CONFIG_WIFI_PASSWORD
#define JSON_URL "https://raw.githubusercontent.com/andrefs/sonotify-nfc-esp32/main/dispatch.json"
#define MAX_HTTP_RECV_BUFFER 4096
#define HA_WEBHOOK_URL "http://192.168.1.32:8123/api/webhook/-dn2X8lTcvihVvBkWAttrVEwZ"
#define SONOS_ENTITY_ID "media_player.roam_2"

static const char *TAG = "wifi_test";
static EventGroupHandle_t wifi_event_group;
#define WIFI_CONNECTED_BIT BIT0
#define WIFI_FAIL_BIT BIT1
static int retry_num = 0;
#define MAX_RETRY 5

#define MAX_HTTP_RECV_BUFFER 4096

bool init_spiffs(void) {
  esp_vfs_spiffs_conf_t conf = {.base_path = "/spiffs",
                                .partition_label = "spiffs",
                                .max_files = 5,
                                .format_if_mount_failed = true};

  esp_err_t ret = esp_vfs_spiffs_register(&conf);
  if (ret != ESP_OK) {
    if (ret == ESP_FAIL) {
      ESP_LOGE(TAG, "Failed to mount or format SPIFFS");
    } else if (ret == ESP_ERR_NOT_FOUND) {
      ESP_LOGE(TAG, "SPIFFS partition not found");
    } else {
      ESP_LOGE(TAG, "Failed to initialize SPIFFS (%s)", esp_err_to_name(ret));
    }
    return false;
  }

  size_t total = 0, used = 0;
  esp_spiffs_info(NULL, &total, &used);
  ESP_LOGI(TAG, "SPIFFS total: %d, used: %d", total, used);
  return true;
}

char *download_json(void) {
  esp_http_client_config_t config = {
      .url = JSON_URL,
      .method = HTTP_METHOD_GET,
      .timeout_ms = 10000,
      .crt_bundle_attach = esp_crt_bundle_attach,
      .disable_auto_redirect = false,
  };

  esp_http_client_handle_t client = esp_http_client_init(&config);
  if (!client) {
    ESP_LOGE(TAG, "Failed to init HTTP client");
    return NULL;
  }

  // Open the connection manually
  esp_err_t err = esp_http_client_open(client, 0);
  if (err != ESP_OK) {
    ESP_LOGE(TAG, "Failed to open HTTP connection: %s", esp_err_to_name(err));
    esp_http_client_cleanup(client);
    return NULL;
  }

  int status = esp_http_client_get_status_code(client);
  ESP_LOGI(TAG, "HTTP status code: %d", status);
  if (status != 200) {
    ESP_LOGE(TAG, "Server returned non-200 status");
    esp_http_client_close(client);
    esp_http_client_cleanup(client);
    return NULL;
  }

  char *buffer = malloc(MAX_HTTP_RECV_BUFFER);
  if (!buffer) {
    ESP_LOGE(TAG, "Out of memory");
    esp_http_client_close(client);
    esp_http_client_cleanup(client);
    return NULL;
  }

  int total_len = 0;
  int read_len;
  while ((read_len =
              esp_http_client_read(client, buffer + total_len,
                                   MAX_HTTP_RECV_BUFFER - total_len - 1)) > 0) {
    total_len += read_len;
    if (total_len >= MAX_HTTP_RECV_BUFFER - 1) {
      ESP_LOGW(TAG, "Buffer full, JSON truncated");
      break;
    }
  }

  buffer[total_len] = '\0';
  ESP_LOGI(TAG, "Downloaded JSON length: %d", total_len);
  ESP_LOGI(TAG, "Downloaded JSON: %s", buffer);

  esp_http_client_close(client);
  esp_http_client_cleanup(client);

  if (total_len == 0) {
    free(buffer);
    return NULL;
  }

  return buffer;
}


char* read_json_file(const char* filename)
{
    FILE* f = fopen(filename, "r");
    if (!f) {
        ESP_LOGE(TAG, "Failed to open file: %s", filename);
        return NULL;
    }

    fseek(f, 0, SEEK_END);
    long fsize = ftell(f);
    fseek(f, 0, SEEK_SET);

    char* json = malloc(fsize + 1);
    if (!json) {
        ESP_LOGE(TAG, "Out of memory");
        fclose(f);
        return NULL;
    }

    fread(json, 1, fsize, f);
    json[fsize] = '\0';
    fclose(f);

    ESP_LOGI(TAG, "Read JSON (%ld bytes): %s", fsize, json);
    return json;
}

bool select_entity(const char *json, char *out_spotify_uri, size_t uri_len) {
  cJSON *root = cJSON_Parse(json);
  if (!root || !cJSON_IsArray(root)) {
    ESP_LOGE(TAG, "Failed to parse JSON");
    cJSON_Delete(root);
    return false;
  }

  cJSON *item = cJSON_GetArrayItem(root, 0);
  if (!item) {
    ESP_LOGE(TAG, "No items in JSON array");
    cJSON_Delete(root);
    return false;
  }

  cJSON *spotify_uri = cJSON_GetObjectItem(item, "spotifyUri");
  if (!cJSON_IsString(spotify_uri)) {
    ESP_LOGE(TAG, "Invalid JSON structure");
    cJSON_Delete(root);
    return false;
  }

  // Copy spotifyUri
  strncpy(out_spotify_uri, spotify_uri->valuestring, uri_len);

  cJSON_Delete(root);
  return true;
}

static void wifi_event_handler(void *arg, esp_event_base_t event_base,
                               int32_t event_id, void *event_data) {
  if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_START) {
    esp_wifi_connect();
  } else if (event_base == WIFI_EVENT &&
             event_id == WIFI_EVENT_STA_DISCONNECTED) {
    if (retry_num < MAX_RETRY) {
      esp_wifi_connect();
      retry_num++;
      ESP_LOGI(TAG, "Retrying connection...");
    } else {
      xEventGroupSetBits(wifi_event_group, WIFI_FAIL_BIT);
    }
  } else if (event_base == IP_EVENT && event_id == IP_EVENT_STA_GOT_IP) {
    ip_event_got_ip_t *event = (ip_event_got_ip_t *)event_data;
    ESP_LOGI(TAG, "Got IP:" IPSTR, IP2STR(&event->ip_info.ip));
    retry_num = 0;
    xEventGroupSetBits(wifi_event_group, WIFI_CONNECTED_BIT);
  }
}

void send_http_post(const char *url, const char *spotify_uri) {
  // Build POST body
  char post_data[256];
  snprintf(post_data, sizeof(post_data), "uri=%s&entity_id=%s", spotify_uri,
           SONOS_ENTITY_ID);

  esp_http_client_config_t config = {
      .url = url,
      .method = HTTP_METHOD_POST,
  };

  esp_http_client_handle_t client = esp_http_client_init(&config);
  esp_http_client_set_header(client, "Content-Type",
                             "application/x-www-form-urlencoded");
  esp_http_client_set_post_field(client, post_data, strlen(post_data));

  esp_err_t err = esp_http_client_perform(client);
  if (err == ESP_OK) {
    ESP_LOGI("HTTP", "POST Status = %d, content_length = %d",
             esp_http_client_get_status_code(client),
             esp_http_client_get_content_length(client));
  } else {
    ESP_LOGE("HTTP", "HTTP POST request failed: %s", esp_err_to_name(err));
  }

  esp_http_client_cleanup(client);
}

void setup_wifi() {
  // Wi-Fi setup code here (omitted for brevity)
  esp_netif_create_default_wifi_sta();

  wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
  ESP_ERROR_CHECK(esp_wifi_init(&cfg));

  esp_event_handler_instance_t instance_any_id;
  esp_event_handler_instance_t instance_got_ip;

  ESP_ERROR_CHECK(esp_event_handler_instance_register(
      WIFI_EVENT, ESP_EVENT_ANY_ID, &wifi_event_handler, NULL,
      &instance_any_id));
  ESP_ERROR_CHECK(esp_event_handler_instance_register(
      IP_EVENT, IP_EVENT_STA_GOT_IP, &wifi_event_handler, NULL,
      &instance_got_ip));

  wifi_config_t wifi_config = {
      .sta =
          {
              .ssid = WIFI_SSID,
              .password = WIFI_PASS,
              .threshold.authmode = WIFI_AUTH_WPA2_PSK,
          },
  };

  ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
  ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &wifi_config));
  ESP_ERROR_CHECK(esp_wifi_start());

  ESP_LOGI(TAG, "Wi-Fi initialization finished.");

  wifi_event_group = xEventGroupCreate();
  EventBits_t bits = xEventGroupWaitBits(
      wifi_event_group, WIFI_CONNECTED_BIT | WIFI_FAIL_BIT, pdFALSE, pdFALSE,
      pdMS_TO_TICKS(10000) // wait up to 10 seconds
  );

  if (bits & WIFI_CONNECTED_BIT) {
    ESP_LOGI(TAG, "Connected to SSID:%s", WIFI_SSID);
  } else if (bits & WIFI_FAIL_BIT) {
    ESP_LOGI(TAG, "Failed to connect to SSID:%s", WIFI_SSID);
  } else {
    ESP_LOGI(TAG, "Wi-Fi connection timed out");
  }
}

void app_main(void) {
  ESP_ERROR_CHECK(nvs_flash_init());
  ESP_ERROR_CHECK(esp_netif_init());
  ESP_ERROR_CHECK(esp_event_loop_create_default());

  setup_wifi();

  // Wait a bit to ensure Wi-Fi is connected
  vTaskDelay(pdMS_TO_TICKS(2000));

  init_spiffs();

  char spotify_uri[128];

  char *json = read_json_file("/spiffs/dispatch.json");
  if(json){
    if (select_entity(json, spotify_uri, sizeof(spotify_uri))) {
      ESP_LOGI(TAG, "Selected Spotify URI: %s", spotify_uri);
      ESP_LOGI(TAG, "SONOS Entity ID: %s", SONOS_ENTITY_ID);
      send_http_post(HA_WEBHOOK_URL, spotify_uri);
    } else {
      ESP_LOGE(TAG, "Failed to select entity from JSON");
    }
  }
  
  free(json);
  return;
}
