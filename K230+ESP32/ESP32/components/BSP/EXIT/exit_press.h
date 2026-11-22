#pragma once

#include <stdbool.h>
#include <stdint.h>

#define EXIT_LONG_PRESS_MS 3000U
typedef enum {
    EXIT_PRESS_SHORT,
    EXIT_PRESS_LONG,
} exit_press_type_t;

typedef enum {
    EXIT_WIFI_MODE_STA,
    EXIT_WIFI_MODE_PROVISIONING,
} exit_wifi_mode_t;

static inline exit_press_type_t exit_classify_press(uint32_t held_ms)
{
    return held_ms >= EXIT_LONG_PRESS_MS ? EXIT_PRESS_LONG : EXIT_PRESS_SHORT;
}

static inline bool exit_press_switches_mode(exit_press_type_t press_type)
{
    return press_type == EXIT_PRESS_LONG;
}

static inline exit_wifi_mode_t exit_next_mode(bool provisioning_active)
{
    return provisioning_active ? EXIT_WIFI_MODE_STA : EXIT_WIFI_MODE_PROVISIONING;
}
