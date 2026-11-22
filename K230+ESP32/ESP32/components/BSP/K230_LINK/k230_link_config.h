#pragma once

#include "driver/uart.h"

#define K230_LINK_UART_NUM          UART_NUM_1
/* GOOUUU ESP32-S3-CAM V1.3: right-side header exposes G1/G2. */
#define K230_LINK_TX_GPIO           1
#define K230_LINK_RX_GPIO           2
#define K230_LINK_BAUD_RATE         921600
#define K230_LINK_RX_BUFFER_SIZE    (16 * 1024)
#define K230_LINK_TX_BUFFER_SIZE    (4 * 1024)
#define K230_LINK_TASK_STACK_SIZE   4096
#define K230_LINK_TASK_PRIORITY     8
#define K230_LINK_HEARTBEAT_MS      2000
#define K230_LINK_OFFLINE_MS        6000
