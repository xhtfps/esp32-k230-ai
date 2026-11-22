#ifndef __LED_H_
#define __LED_H_

#include <stdint.h>
#include "esp_check.h"
#include "led_strip.h"

#define LED_GPIO_PIN 48

typedef enum {
    LED_COLOR_ID_RED,
    LED_COLOR_ID_GREEN,
    LED_COLOR_ID_BLUE,
    LED_COLOR_ID_YELLOW,
    LED_COLOR_ID_PURPLE,
    LED_COLOR_ID_CYAN,
    LED_COLOR_ID_WHITE,
    LED_COLOR_ID_ORANGE,
    LED_COLOR_ID_PINK,
    LED_COLOR_ID_WARM_WHITE,
    LED_COLOR_ID_COOL_WHITE,
    LED_COLOR_ID_TEAL,
    LED_COLOR_ID_SKY_BLUE,
    LED_COLOR_ID_DEEP_BLUE,
    LED_COLOR_ID_INDIGO,
    LED_COLOR_ID_VIOLET,
    LED_COLOR_ID_MAGENTA,
    LED_COLOR_ID_ROSE,
    LED_COLOR_ID_GOLD,
    LED_COLOR_ID_SILVER,
    LED_COLOR_ID_BROWN,
    LED_COLOR_ID_GRAY,
    LED_COLOR_ID_BLACK,
    LED_COLOR_ID_COUNT,
} led_color_id_t;

#define LED_COLOR_RED        LED_COLOR_ID_RED
#define LED_COLOR_GREEN      LED_COLOR_ID_GREEN
#define LED_COLOR_BLUE       LED_COLOR_ID_BLUE
#define LED_COLOR_YELLOW     LED_COLOR_ID_YELLOW
#define LED_COLOR_PURPLE     LED_COLOR_ID_PURPLE
#define LED_COLOR_CYAN       LED_COLOR_ID_CYAN
#define LED_COLOR_WHITE      LED_COLOR_ID_WHITE
#define LED_COLOR_ORANGE     LED_COLOR_ID_ORANGE
#define LED_COLOR_PINK       LED_COLOR_ID_PINK
#define LED_COLOR_WARM_WHITE LED_COLOR_ID_WARM_WHITE
#define LED_COLOR_COOL_WHITE LED_COLOR_ID_COOL_WHITE
#define LED_COLOR_TEAL       LED_COLOR_ID_TEAL
#define LED_COLOR_SKY_BLUE   LED_COLOR_ID_SKY_BLUE
#define LED_COLOR_DEEP_BLUE  LED_COLOR_ID_DEEP_BLUE
#define LED_COLOR_INDIGO     LED_COLOR_ID_INDIGO
#define LED_COLOR_VIOLET     LED_COLOR_ID_VIOLET
#define LED_COLOR_MAGENTA    LED_COLOR_ID_MAGENTA
#define LED_COLOR_ROSE       LED_COLOR_ID_ROSE
#define LED_COLOR_GOLD       LED_COLOR_ID_GOLD
#define LED_COLOR_SILVER     LED_COLOR_ID_SILVER
#define LED_COLOR_BROWN      LED_COLOR_ID_BROWN
#define LED_COLOR_GRAY       LED_COLOR_ID_GRAY
#define LED_COLOR_BLACK      LED_COLOR_ID_BLACK

void led_init(void);
void led_set_color(uint8_t red, uint8_t green, uint8_t blue);
void led_set_preset_color(led_color_id_t color);
void led_next_color(void);
void led_set_wifi_connecting(void);
void led_set_wifi_connected(void);
void led_set_wifi_failed(void);
void led_set_wifi_provisioning(void);

#define LED_TOGGLE() led_next_color()

#endif
