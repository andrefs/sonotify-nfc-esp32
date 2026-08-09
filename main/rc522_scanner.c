#include "rc522_scanner.h"

#include "driver/gpio.h"
#include "driver/rc522_spi.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "rc522.h"
#include "rc522_picc.h"

#include "dispatch.h"
#include "ha_client.h"
#include "led.h"

static const char *TAG = "rc522";

// --- Pin definitions (configured via menuconfig) ---
#define RC522_SPI_BUS_GPIO_MISO CONFIG_RC522_SPI_BUS_GPIO_MISO
#define RC522_SPI_BUS_GPIO_MOSI CONFIG_RC522_SPI_BUS_GPIO_MOSI
#define RC522_SPI_BUS_GPIO_SCLK CONFIG_RC522_SPI_BUS_GPIO_SCLK
#define RC522_SPI_SCANNER_GPIO_SDA CONFIG_RC522_SPI_SCANNER_GPIO_SDA
#define RC522_SCANNER_GPIO_RST CONFIG_RC522_SCANNER_GPIO_RST

// enough for 10-byte UID + separators + null
#define RC522_PICC_UID_HEXSTR_MAX 32
#define RC522_START_RETRIES 3
#define RC522_START_RETRY_DELAY_MS 100

static const char *s_json;

static rc522_spi_config_t s_driver_config = {
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

static rc522_driver_handle_t s_driver;
static rc522_handle_t s_scanner;

static const char *rc522_get_hexstr(const rc522_picc_t *picc) {
  static char uid_str[RC522_PICC_UID_HEXSTR_MAX] = {0};
  if (!picc) {
    return NULL;
  }

  // rc522_picc_uid_to_str returns esp_err_t, ignore error for simplicity
  rc522_picc_uid_to_str(&picc->uid, uid_str, sizeof(uid_str));
  return uid_str;
}

static void on_picc_state_changed(void *arg, esp_event_base_t base,
                                  int32_t event_id, void *data) {
  rc522_picc_state_changed_event_t *event =
      (rc522_picc_state_changed_event_t *)data;
  rc522_picc_t *picc = event->picc;

  if (picc->state == RC522_PICC_STATE_ACTIVE ||
      picc->state == RC522_PICC_STATE_ACTIVE_H) {

    led_set(true); // LED ON

    const char *uid_hex = rc522_get_hexstr(picc);
    ESP_LOGI(TAG, "Card UID: %s", uid_hex);

    char content_id[128];
    char content_type[64];
    if (dispatch_lookup(s_json, uid_hex, content_id, sizeof(content_id),
                        content_type, sizeof(content_type))) {
      ha_client_send_webhook(CONFIG_HA_WEBHOOK_URL, content_id, content_type);
    } else {
      ESP_LOGW(TAG,
               "Card not in dispatch.json. Add an entry with tagId \"%s\" to "
               "add it",
               uid_hex);
    }

  } else if (picc->state == RC522_PICC_STATE_IDLE &&
             event->old_state >= RC522_PICC_STATE_ACTIVE) {
    led_set(false); // LED OFF
    ESP_LOGI(TAG, "Card has been removed");
  }
}

esp_err_t rc522_scanner_init(const char *dispatch_json) {
  if (!dispatch_json) {
    return ESP_ERR_INVALID_ARG;
  }
  s_json = dispatch_json;

  esp_err_t ret;

  // --- Hardware reset ---
  gpio_reset_pin(s_driver_config.rst_io_num);
  gpio_set_direction(s_driver_config.rst_io_num, GPIO_MODE_OUTPUT);
  gpio_set_level(s_driver_config.rst_io_num, 0);
  vTaskDelay(pdMS_TO_TICKS(50));
  gpio_set_level(s_driver_config.rst_io_num, 1);
  vTaskDelay(pdMS_TO_TICKS(50));

  // --- Create SPI driver ---
  ret = rc522_spi_create(&s_driver_config, &s_driver);
  ESP_ERROR_CHECK(ret);

  // --- Install driver ---
  ret = rc522_driver_install(s_driver);
  ESP_ERROR_CHECK(ret);

  // --- Create scanner ---
  rc522_config_t scanner_config = {
      .driver = s_driver,
  };
  ret = rc522_create(&scanner_config, &s_scanner);
  ESP_ERROR_CHECK(ret);

  // --- Register event handler ---
  rc522_register_events(s_scanner, RC522_EVENT_PICC_STATE_CHANGED,
                        on_picc_state_changed, NULL);

  // --- Start scanning (retry on intermittent FIFO self-test failure) ---
  for (int attempt = 1; attempt <= RC522_START_RETRIES; attempt++) {
    ret = rc522_start(s_scanner);
    if (ret == ESP_OK) {
      break;
    }
    ESP_LOGW(TAG, "rc522_start failed (attempt %d/%d): %s", attempt,
             RC522_START_RETRIES, esp_err_to_name(ret));
    vTaskDelay(pdMS_TO_TICKS(RC522_START_RETRY_DELAY_MS));
  }

  if (ret == ESP_OK) {
    ESP_LOGI(TAG, "Scanning started");
  } else {
    ESP_LOGE(TAG, "Failed to start scanning after %d attempts",
             RC522_START_RETRIES);
  }
  return ret;
}