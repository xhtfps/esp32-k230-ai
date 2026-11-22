#pragma once

#include <stddef.h>
#include <stdint.h>

#include "driver/uart.h"
#include "esp_err.h"

#define SU03T_LINK_DEFAULT_UART_NUM UART_NUM_2
#define SU03T_LINK_DEFAULT_TX_GPIO  19
#define SU03T_LINK_DEFAULT_RX_GPIO  20
#define SU03T_LINK_DEFAULT_BAUD    9600
#define SU03T_LINK_MAX_PARAMS      32

typedef struct {
    uint8_t msgno;
    uint8_t params[SU03T_LINK_MAX_PARAMS];
    size_t param_length;
} su03t_event_t;

typedef void (*su03t_event_callback_t)(const su03t_event_t *event, void *context);

typedef struct {
    uart_port_t uart_num;
    int tx_gpio;
    int rx_gpio;
    int baud_rate;
    int rx_buffer_size;
    int task_stack_size;
    int task_priority;
} su03t_link_config_t;

esp_err_t su03t_link_start(const su03t_link_config_t *config);
void su03t_link_set_event_callback(su03t_event_callback_t callback, void *context);
