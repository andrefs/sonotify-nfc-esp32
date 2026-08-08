#ifndef LED_H_
#define LED_H_

#include <stdbool.h>

typedef enum {
    LED_BLINK_OFF = 0,  // steady off
    LED_BLINK_SLOW = 1, // Wi-Fi connecting
    LED_BLINK_FAST = 2, // Wi-Fi failed / HTTP error
} led_blink_mode_t;

void led_init(void);
void led_set(bool level);
void led_set_blink_mode(led_blink_mode_t mode);
void led_start_blink_task(void);

#endif /* LED_H_ */