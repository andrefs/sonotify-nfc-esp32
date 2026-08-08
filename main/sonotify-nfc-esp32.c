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

#include "driver/rc522_spi.h"
#include "rc522.h"
#include "rc522_picc.h"
#include <driver/gpio.h>
#include <esp_log.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

// --- Pin definitions (configured via menuconfig) ---
#define RC522_SPI_BUS_GPIO_MISO CONFIG_RC522_SPI_BUS_GPIO_MISO
#define RC522_SPI_BUS_GPIO_MOSI CONFIG_RC522_SPI_BUS_GPIO_MOSI
#define RC522_SPI_BUS_GPIO_SCLK CONFIG_RC522_SPI_BUS_GPIO_SCLK
#define RC522_SPI_SCANNER_GPIO_SDA CONFIG_RC522_SPI_SCANNER_GPIO_SDA
#define RC522_SCANNER_GPIO_RST CONFIG_RC522_SCANNER_GPIO_RST

#define LED_GPIO ((gpio_num_t)CONFIG_LED_GPIO)
#define LED_BLINK_OFF 0
#define LED_BLINK_SLOW 1 // Wi-Fi connecting
#define LED_BLINK_FAST 2 // HTTP error
static volatile int led_blink_mode = LED_BLINK_OFF;

// enough for 10-byte UID + separators + null
#define RC522_PICC_UID_HEXSTR_MAX 32

static const char *TAG = "wifi_test";
char *json;
char content_id[128];
char content_type[64];

/**
 * @brief Get the UID of a card as a hex string.
 * @param picc Pointer to rc522_picc_t
 * @return Pointer to static buffer containing hex string (colon-separated)
 */
const char *rc522_get_hexstr(const rc522_picc_t *picc) {
  static char uid_str[RC522_PICC_UID_HEXSTR_MAX] = {0};
  if (!picc) {
    return NULL;
  }

  // rc522_picc_uid_to_str returns esp_err_t, ignore error for simplicity
  rc522_picc_uid_to_str(&picc->uid, uid_str, sizeof(uid_str));
  return uid_str;
}

// --- Driver and scanner handles ---
static rc522_spi_config_t driver_config = {
    .host_id = SPI3_HOST,
    .bus_config = &(spi_bus_config_t){.miso_io_num = RC522_SPI_BUS_GPIO_MISO,
                                      .mosi_io_num = RC522_SPI_BUS_GPIO_MOSI,
                                      .sclk_io_num = RC522_SPI_BUS_GPIO_SCLK,
                                      .quadwp_io_num = -1,
                                      .quadhd_io_num = -1,
                                      .max_transfer_sz = 0},
    .dev_config = {.spics_io_num = RC522_SPI_SCANNER_GPIO_SDA,
                   .clock_speed_hz = CONFIG_RC522_SPI_CLOCK_HZ},
    .rst_io_num = RC522_SCANNER_GPIO_RST,
};

static rc522_driver_handle_t driver;
static rc522_handle_t scanner;

// Parse JSON and select entity based on UID matching item .tagId
bool select_entity(const char *json, size_t id_len, char *uid_hex) {
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
           contentId ? contentId->valuestring : "null");
  ESP_LOGI(TAG, "Description: %s",
           description ? description->valuestring : "null");
  ESP_LOGI(TAG, "ContentType: %s",
           contentType ? contentType->valuestring : "null");
  if (!cJSON_IsString(contentId)) {
    ESP_LOGE(TAG, "contentId not found or invalid");
  }

  strncpy(content_id, contentId->valuestring, id_len - 1);
  content_id[id_len - 1] = '\0';

  if (cJSON_IsString(contentType)) {
    strncpy(content_type, contentType->valuestring, sizeof(content_type) - 1);
    content_type[sizeof(content_type) - 1] = '\0';
  } else {
    strncpy(content_type, "music", sizeof(content_type) - 1);
    content_type[sizeof(content_type) - 1] = '\0';
  }

  ESP_LOGI(TAG, "Selected content id: %s (%s) %s", content_id,
           description->valuestring, contentType->valuestring);
  ESP_LOGI(TAG, "SONOS Entity ID: %s", CONFIG_SONOS_ENTITY_ID);

  cJSON_Delete(root);
  return true;
}

void send_http_post(const char *url, const char *content_id,
                    const char *content_type) {
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
  esp_http_client_set_header(client, "Content-Type",
                             "application/x-www-form-urlencoded");
  esp_http_client_set_post_field(client, post_data, strlen(post_data));

  esp_err_t err = esp_http_client_perform(client);
  if (err == ESP_OK) {
    ESP_LOGI("HTTP", "POST Status = %d, content_length = %d",
             esp_http_client_get_status_code(client),
             esp_http_client_get_content_length(client));
    led_blink_mode = LED_BLINK_OFF; // successful request
  } else {
    ESP_LOGE("HTTP", "HTTP POST request failed: %s", esp_err_to_name(err));
    led_blink_mode = LED_BLINK_FAST; // indicate error
  }

  esp_http_client_cleanup(client);
}

// --- Event callback ---
static void on_picc_state_changed(void *arg, esp_event_base_t base,
                                  int32_t event_id, void *data) {
  rc522_picc_state_changed_event_t *event =
      (rc522_picc_state_changed_event_t *)data;
  rc522_picc_t *picc = event->picc;

  if (picc->state == RC522_PICC_STATE_ACTIVE ||
      picc->state == RC522_PICC_STATE_ACTIVE_H) {

    gpio_set_level(LED_GPIO, 1); // LED ON

    const char *uid_hex = rc522_get_hexstr(picc);
    ESP_LOGI("RC522", "Card UID: %s", uid_hex);

    if (select_entity(json, sizeof(content_id), (char *)uid_hex)) {
      send_http_post(CONFIG_HA_WEBHOOK_URL, content_id, content_type);
    } else {
      ESP_LOGE(TAG, "Failed to select entity from JSON");
    }

  } else if (picc->state == RC522_PICC_STATE_IDLE &&
             event->old_state >= RC522_PICC_STATE_ACTIVE) {
    gpio_set_level(LED_GPIO, 0); // LED OFF
    ESP_LOGI("RC522", "Card has been removed");
  }
}

// ===============================
// WIFI
// ===============================

#define WIFI_SSID CONFIG_WIFI_SSID
#define WIFI_PASS CONFIG_WIFI_PASSWORD

static EventGroupHandle_t wifi_event_group;
#define WIFI_CONNECTED_BIT BIT0
#define WIFI_FAIL_BIT BIT1
static int retry_num = 0;
#define MAX_RETRY CONFIG_WIFI_MAX_RETRY

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

char *read_json_file(const char *filename) {
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

static void wifi_event_handler(void *arg, esp_event_base_t event_base,
                               int32_t event_id, void *event_data) {
  if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_START) {
    led_blink_mode = LED_BLINK_SLOW; // Wi-Fi connecting
    esp_wifi_connect();
  } else if (event_base == WIFI_EVENT &&
             event_id == WIFI_EVENT_STA_DISCONNECTED) {
    if (retry_num < MAX_RETRY) {
      led_blink_mode = LED_BLINK_SLOW; // still connecting
      esp_wifi_connect();
      retry_num++;
      ESP_LOGI(TAG, "Retrying connection...");
    } else {
      led_blink_mode = LED_BLINK_FAST; // Wi-Fi failed
      xEventGroupSetBits(wifi_event_group, WIFI_FAIL_BIT);
    }
  } else if (event_base == IP_EVENT && event_id == IP_EVENT_STA_GOT_IP) {
    led_blink_mode = LED_BLINK_OFF; // connected
    ip_event_got_ip_t *event = (ip_event_got_ip_t *)event_data;
    ESP_LOGI(TAG, "Got IP:" IPSTR, IP2STR(&event->ip_info.ip));
    retry_num = 0;
    xEventGroupSetBits(wifi_event_group, WIFI_CONNECTED_BIT);
  }
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

// ===============================
// LED
// ==============================

static void led_init(void) {
  gpio_reset_pin(LED_GPIO);
  gpio_set_direction(LED_GPIO, GPIO_MODE_OUTPUT);
  gpio_set_level(LED_GPIO, 0); // LED off initially
}

static void led_blink_task(void *arg) {
  int led_state = 0;
  while (1) {
    switch (led_blink_mode) {
    case LED_BLINK_OFF:
      gpio_set_level(LED_GPIO, 0);
      vTaskDelay(pdMS_TO_TICKS(200));
      break;
    case LED_BLINK_SLOW:
      led_state = !led_state;
      gpio_set_level(LED_GPIO, led_state);
      vTaskDelay(pdMS_TO_TICKS(500)); // 0.5 Hz
      break;
    case LED_BLINK_FAST:
      led_state = !led_state;
      gpio_set_level(LED_GPIO, led_state);
      vTaskDelay(pdMS_TO_TICKS(100)); // 5 Hz
      break;
    }
  }
}

// ===============================
// Main application
// ===============================

void app_main(void) {
  led_init();
  gpio_set_level(LED_GPIO, 1); // LED ON

  ESP_ERROR_CHECK(nvs_flash_init());
  ESP_ERROR_CHECK(esp_netif_init());
  ESP_ERROR_CHECK(esp_event_loop_create_default());

  setup_wifi();

  // Wait a bit to ensure Wi-Fi is connected
  vTaskDelay(pdMS_TO_TICKS(2000));

  init_spiffs();

  json = read_json_file("/spiffs/dispatch.json");
  if (!json) {
    ESP_LOGE(TAG, "Failed to read JSON file");
    return;
  }

  xTaskCreate(led_blink_task, "led_blink_task", 1024, NULL, 5, NULL);

  esp_err_t ret;

  // --- Hardware reset ---
  gpio_reset_pin(driver_config.rst_io_num);
  gpio_set_direction(driver_config.rst_io_num, GPIO_MODE_OUTPUT);
  gpio_set_level(driver_config.rst_io_num, 0);
  vTaskDelay(pdMS_TO_TICKS(50));
  gpio_set_level(driver_config.rst_io_num, 1);
  vTaskDelay(pdMS_TO_TICKS(50));

  // --- Create SPI driver ---
  ret = rc522_spi_create(&driver_config, &driver);
  ESP_ERROR_CHECK(ret);

  // --- Install driver ---
  ret = rc522_driver_install(driver);
  ESP_ERROR_CHECK(ret);

  // --- Create scanner ---
  rc522_config_t scanner_config = {
      .driver = driver,
  };
  ret = rc522_create(&scanner_config, &scanner);
  ESP_ERROR_CHECK(ret);

  // --- Register event handler ---
  rc522_register_events(scanner, RC522_EVENT_PICC_STATE_CHANGED,
                        on_picc_state_changed, NULL);

  // --- Start scanning ---
  ret = rc522_start(scanner);
  ESP_LOGI(TAG, "rc522_start returned: %s", esp_err_to_name(ret));

  gpio_set_level(LED_GPIO, 0); // LED ON
  // free(json);
  return;
}
