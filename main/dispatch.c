#include "dispatch.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "cJSON.h"
#include "esp_crt_bundle.h"
#include "esp_http_client.h"
#include "esp_log.h"
#include "esp_spiffs.h"

static const char *TAG = "dispatch";

static bool init_spiffs(void) {
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
  esp_err_t info_ret = esp_spiffs_info("spiffs", &total, &used);
  if (info_ret == ESP_OK) {
    ESP_LOGI(TAG, "SPIFFS total: %zu, used: %zu", total, used);
  } else {
    ESP_LOGW(TAG, "Failed to read SPIFFS info (%s)", esp_err_to_name(info_ret));
  }
  return true;
}

static char *read_json_file(const char *filename) {
  FILE *f = fopen(filename, "r");
  if (!f) {
    ESP_LOGE(TAG, "Failed to open file: %s", filename);
    return NULL;
  }

  fseek(f, 0, SEEK_END);
  long fsize = ftell(f);
  fseek(f, 0, SEEK_SET);

  char *json = malloc(fsize + 1);
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

static bool write_json_file(const char *filename, const char *json) {
  FILE *f = fopen(filename, "w");
  if (!f) {
    ESP_LOGE(TAG, "Failed to open file for writing: %s", filename);
    return false;
  }

  bool ok = fputs(json, f) >= 0;
  if (fclose(f) != 0) {
    ok = false;
  }
  return ok;
}

typedef struct {
  char *data;
  size_t len;
  size_t cap;
} http_body_t;

static esp_err_t http_event_handler(esp_http_client_event_t *event) {
  if (event->event_id != HTTP_EVENT_ON_DATA) {
    return ESP_OK;
  }
  http_body_t *body = (http_body_t *)event->user_data;
  size_t new_len = body->len + event->data_len;
  if (new_len >= body->cap) {
    size_t new_cap = body->cap ? body->cap * 2 : 256;
    while (new_cap < new_len) {
      new_cap *= 2;
    }
    char *new_data = realloc(body->data, new_cap + 1);
    if (!new_data) {
      ESP_LOGE(TAG, "Out of memory while reading HTTP body");
      return ESP_ERR_NO_MEM;
    }
    body->data = new_data;
    body->cap = new_cap;
  }
  memcpy(body->data + body->len, event->data, event->data_len);
  body->len = new_len;
  return ESP_OK;
}

static char *fetch_dispatch_json(const char *url) {
  http_body_t body = {0};
  esp_http_client_config_t config = {
      .url = url,
      .method = HTTP_METHOD_GET,
      .event_handler = http_event_handler,
      .user_data = &body,
      .crt_bundle_attach = esp_crt_bundle_attach,
      .timeout_ms = 5000,
  };

  esp_http_client_handle_t client = esp_http_client_init(&config);
  if (!client) {
    ESP_LOGE(TAG, "Failed to init HTTP client");
    return NULL;
  }

  esp_err_t err = esp_http_client_perform(client);
  int status = esp_http_client_get_status_code(client);
  esp_http_client_cleanup(client);

  if (err != ESP_OK || status != 200 || body.data == NULL) {
    ESP_LOGW(TAG, "GET %s failed: err=%s status=%d", url, esp_err_to_name(err),
             status);
    free(body.data);
    return NULL;
  }

  body.data[body.len] = '\0';
  ESP_LOGI(TAG, "Fetched JSON (%zu bytes): %s", body.len, body.data);
  return body.data;
}

static bool dispatch_sane(const char *json) {
  cJSON *root = cJSON_Parse(json);
  if (!root) {
    ESP_LOGE(TAG, "Fetched dispatch JSON failed to parse");
    return false;
  }

  if (!cJSON_IsArray(root) || cJSON_GetArraySize(root) == 0) {
    ESP_LOGE(TAG, "Fetched dispatch JSON is not a non-empty array");
    cJSON_Delete(root);
    return false;
  }

  cJSON *item;
  cJSON_ArrayForEach(item, root) {
    cJSON *tagId = cJSON_GetObjectItem(item, "tagId");
    cJSON *contentId = cJSON_GetObjectItem(item, "contentId");
    if (!cJSON_IsString(tagId) || !cJSON_IsString(contentId)) {
      ESP_LOGE(TAG, "Fetched dispatch JSON entry missing tagId/contentId");
      cJSON_Delete(root);
      return false;
    }
  }

  cJSON_Delete(root);
  return true;
}

char *dispatch_load(void) {
  if (!init_spiffs()) {
    return NULL;
  }

  const char *url = CONFIG_DISPATCH_SOURCE_URL;
  ESP_LOGI(TAG, "Dispatch JSON source URL: %s", url);
  if (url[0] != '\0') {
    char *fetched = fetch_dispatch_json(url);
    if (fetched != NULL) {
      if (dispatch_sane(fetched)) {
        if (write_json_file("/spiffs/dispatch.json", fetched)) {
          ESP_LOGI(TAG, "Dispatch JSON updated from %s", url);
          return fetched;
        }
        ESP_LOGW(TAG, "Failed to persist fetched dispatch JSON");
      } else {
        ESP_LOGW(TAG, "Fetched dispatch JSON failed sanity check");
      }
      free(fetched);
      ESP_LOGW(TAG, "Falling back to SPIFFS copy");
    } else {
      ESP_LOGW(TAG, "Failed to fetch dispatch JSON from %s, using SPIFFS",
               url);
    }
  }
  return read_json_file("/spiffs/dispatch.json");
}

static void copy_str(const char *src, char *dst, size_t dst_len) {
  if (!src) {
    dst[0] = '\0';
    return;
  }
  strncpy(dst, src, dst_len - 1);
  dst[dst_len - 1] = '\0';
}

bool dispatch_lookup(const char *json, const char *uid_hex,
                     char *out_content_id, size_t content_id_len,
                     char *out_content_type, size_t content_type_len) {
  if (!json || !uid_hex) {
    return false;
  }

  cJSON *root = cJSON_Parse(json);
  if (!root || !cJSON_IsArray(root)) {
    ESP_LOGE(TAG, "Failed to parse JSON");
    cJSON_Delete(root);
    return false;
  }

  cJSON *selected = NULL;
  cJSON *item;
  cJSON_ArrayForEach(item, root) {
    cJSON *tagId = cJSON_GetObjectItem(item, "tagId");
    if (cJSON_IsString(tagId)) {
      if (strcmp(tagId->valuestring, uid_hex) == 0) {
        selected = item;
        break;
      }
    }
  }

  if (!selected) {
    ESP_LOGE(TAG, "No matching entity found for UID: %s", uid_hex);
    cJSON_Delete(root);
    return false;
  }

  cJSON *contentId = cJSON_GetObjectItem(selected, "contentId");
  cJSON *description = cJSON_GetObjectItem(selected, "description");
  cJSON *contentType = cJSON_GetObjectItem(selected, "contentType");

  ESP_LOGI(TAG, "Found entity with contentId: %s",
           cJSON_IsString(contentId) ? contentId->valuestring : "null");
  ESP_LOGI(TAG, "Description: %s",
           cJSON_IsString(description) ? description->valuestring : "null");
  ESP_LOGI(TAG, "ContentType: %s",
           cJSON_IsString(contentType) ? contentType->valuestring : "null");

  if (!cJSON_IsString(contentId)) {
    ESP_LOGE(TAG, "contentId not found or invalid");
    cJSON_Delete(root);
    return false;
  }

  copy_str(contentId->valuestring, out_content_id, content_id_len);
  if (cJSON_IsString(contentType)) {
    copy_str(contentType->valuestring, out_content_type, content_type_len);
  } else {
    copy_str("music", out_content_type, content_type_len);
  }

  ESP_LOGI(TAG, "Selected content id: %s (%s) %s", out_content_id,
           cJSON_IsString(description) ? description->valuestring : "null",
           out_content_type);
  ESP_LOGI(TAG, "SONOS Entity ID: %s", CONFIG_SONOS_ENTITY_ID);

  cJSON_Delete(root);
  return true;
}