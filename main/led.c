#include "led.h"

#include "driver/gpio.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#define LED_GPIO ((gpio_num_t)CONFIG_LED_GPIO)

static volatile led_blink_mode_t s_led_mode = LED_BLINK_OFF;

void led_init(void) {
  gpio_reset_pin(LED_GPIO);
  gpio_set_direction(LED_GPIO, GPIO_MODE_OUTPUT);
  gpio_set_level(LED_GPIO, 0); // LED off initially
}

void led_set(bool level) { gpio_set_level(LED_GPIO, level ? 1 : 0); }

void led_set_blink_mode(led_blink_mode_t mode) { s_led_mode = mode; }

static void led_blink_task(void *arg) {
  int led_state = 0;
  while (1) {
    switch (s_led_mode) {
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

void led_start_blink_task(void) {
  xTaskCreate(led_blink_task, "led_blink_task", 1024, NULL, 5, NULL);
}