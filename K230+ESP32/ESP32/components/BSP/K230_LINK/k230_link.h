#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "esp_err.h"
#include "k230_protocol.h"

typedef void (*k230_link_frame_callback_t)(const k230_protocol_frame_t *frame, void *context);

typedef struct {
    int uart_num;
    int tx_gpio;
    int rx_gpio;
    int baud_rate;
    int rx_buffer_size;
    int tx_buffer_size;
    int task_stack_size;
    int task_priority;
} k230_link_config_t;

esp_err_t k230_link_start(const k230_link_config_t *config);
esp_err_t k230_link_send(uint8_t type, const uint8_t *payload, size_t length);
esp_err_t k230_link_request_jpeg(uint16_t request_id);
void k230_link_set_frame_callback(k230_link_frame_callback_t callback, void *context);
bool k230_link_is_online(void);
int64_t k230_link_last_rx_us(void);
