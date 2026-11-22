#pragma once

#include <stdint.h>

#include "driver/uart.h"
#include "esp_err.h"

#define ASRPRO_LINK_DEFAULT_UART_NUM UART_NUM_2
#define ASRPRO_LINK_DEFAULT_TX_GPIO  19
#define ASRPRO_LINK_DEFAULT_RX_GPIO  20
#define ASRPRO_LINK_DEFAULT_BAUD     9600

typedef struct {
    uint8_t command_id;
} asrpro_event_t;

typedef void (*asrpro_event_callback_t)(const asrpro_event_t *event, void *context);

typedef struct {
    uart_port_t uart_num;
    int tx_gpio;
    int rx_gpio;
    int baud_rate;
    int rx_buffer_size;
    int task_stack_size;
    int task_priority;
} asrpro_link_config_t;

esp_err_t asrpro_link_start(const asrpro_link_config_t *config);
void asrpro_link_set_event_callback(asrpro_event_callback_t callback, void *context);
