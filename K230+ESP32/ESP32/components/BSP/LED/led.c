#include "led.h"

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#define LED_STRIP_RESOLUTION_HZ  (10 * 1000 * 1000)

typedef struct {
    uint8_t red;
    uint8_t green;
    uint8_t blue;
} led_color_t;

static led_strip_handle_t s_led_strip;
static size_t s_next_color_index;
static TaskHandle_t s_wifi_connecting_task;

static const led_color_t s_colors[LED_COLOR_ID_COUNT] = {
    [LED_COLOR_ID_RED]        = { 32, 0, 0 },
    [LED_COLOR_ID_GREEN]      = { 0, 32, 0 },
    [LED_COLOR_ID_BLUE]       = { 0, 0, 32 },
    [LED_COLOR_ID_YELLOW]     = { 32, 24, 0 },
    [LED_COLOR_ID_PURPLE]     = { 24, 0, 32 },
    [LED_COLOR_ID_CYAN]       = { 0, 24, 32 },
    [LED_COLOR_ID_WHITE]      = { 24, 24, 24 },
    [LED_COLOR_ID_ORANGE]     = { 32, 10, 0 },
    [LED_COLOR_ID_PINK]       = { 32, 4, 12 },
    [LED_COLOR_ID_WARM_WHITE] = { 32, 16, 6 },
    [LED_COLOR_ID_COOL_WHITE] = { 18, 24, 32 },
    [LED_COLOR_ID_TEAL]       = { 0, 24, 16 },
    [LED_COLOR_ID_SKY_BLUE]   = { 0, 16, 32 },
    [LED_COLOR_ID_DEEP_BLUE]  = { 0, 0, 16 },
    [LED_COLOR_ID_INDIGO]     = { 8, 0, 24 },
    [LED_COLOR_ID_VIOLET]     = { 18, 0, 28 },
    [LED_COLOR_ID_MAGENTA]    = { 32, 0, 20 },
    [LED_COLOR_ID_ROSE]       = { 32, 0, 6 },
    [LED_COLOR_ID_GOLD]       = { 32, 18, 0 },
    [LED_COLOR_ID_SILVER]     = { 16, 16, 20 },
    [LED_COLOR_ID_BROWN]      = { 18, 6, 0 },
    [LED_COLOR_ID_GRAY]       = { 8, 8, 8 },
    [LED_COLOR_ID_BLACK]      = { 0, 0, 0 },
};

void led_init(void)
{
    led_strip_config_t strip_config = {
        .strip_gpio_num = LED_GPIO_PIN,
        .max_leds = 1,
    };
    led_strip_rmt_config_t rmt_config = {
        .resolution_hz = LED_STRIP_RESOLUTION_HZ,
        .flags.with_dma = false,
    };

    ESP_ERROR_CHECK(led_strip_new_rmt_device(&strip_config, &rmt_config, &s_led_strip));
    ESP_ERROR_CHECK(led_strip_clear(s_led_strip));
}

void led_set_color(uint8_t red, uint8_t green, uint8_t blue)
{
    ESP_ERROR_CHECK(led_strip_set_pixel(s_led_strip, 0, red, green, blue));
    ESP_ERROR_CHECK(led_strip_refresh(s_led_strip));
}

void led_set_preset_color(led_color_id_t color)
{
    if ((unsigned int)color >= LED_COLOR_ID_COUNT) {
        return;
    }

    led_set_color(s_colors[color].red, s_colors[color].green, s_colors[color].blue);
}

void led_next_color(void)
{
    led_set_preset_color((led_color_id_t)s_next_color_index);
    s_next_color_index = (s_next_color_index + 1) % LED_COLOR_ID_BLACK;
}

static void led_wifi_connecting_task(void *arg)
{
    (void)arg;

    while (true) {
        led_set_preset_color(LED_COLOR_RED);
        vTaskDelay(pdMS_TO_TICKS(500));
        led_set_preset_color(LED_COLOR_BLUE);
        vTaskDelay(pdMS_TO_TICKS(500));
    }
}

static void led_stop_wifi_connecting_task(void)
{
    if (s_wifi_connecting_task != NULL) {
        vTaskDelete(s_wifi_connecting_task);
        s_wifi_connecting_task = NULL;
    }
}

void led_set_wifi_connecting(void)
{
    if (s_wifi_connecting_task == NULL) {
        xTaskCreate(led_wifi_connecting_task, "wifi_led", 2048, NULL, 4, &s_wifi_connecting_task);
    }
}

void led_set_wifi_connected(void)
{
    led_stop_wifi_connecting_task();
    led_set_preset_color(LED_COLOR_BLUE);
}

void led_set_wifi_failed(void)
{
    led_stop_wifi_connecting_task();
    led_set_preset_color(LED_COLOR_RED);
}

void led_set_wifi_provisioning(void)
{
    led_stop_wifi_connecting_task();
    led_set_preset_color(LED_COLOR_GREEN);
}
