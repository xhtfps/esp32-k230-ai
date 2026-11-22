#include "su03t_link.h"

#include <stdbool.h>
#include <string.h>

#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static const char *TAG = "SU03T_LINK";

#define SU03T_FRAME_MAX_SIZE (SU03T_LINK_MAX_PARAMS + 5U)

static bool s_started;
static uart_port_t s_uart_num = SU03T_LINK_DEFAULT_UART_NUM;
static su03t_event_callback_t s_event_callback;
static void *s_event_context;

static void su03t_parser_reset(size_t *length, bool *in_frame)
{
    *length = 0U;
    *in_frame = false;
}

static void su03t_parser_feed(uint8_t byte, uint8_t *frame, size_t *length,
                              bool *in_frame)
{
    if (!*in_frame) {
        if (byte == 0xAAU) {
            frame[0] = byte;
            *length = 1U;
            *in_frame = true;
        }
        return;
    }

    if (*length == 1U && byte != 0x55U) {
        if (byte == 0xAAU) {
            frame[0] = byte;
            *length = 1U;
        } else {
            su03t_parser_reset(length, in_frame);
        }
        return;
    }

    if (*length >= SU03T_FRAME_MAX_SIZE) {
        su03t_parser_reset(length, in_frame);
        return;
    }

    frame[(*length)++] = byte;
    if (*length >= 5U && frame[*length - 2U] == 0x55U &&
        frame[*length - 1U] == 0xAAU) {
        size_t param_length = *length - 5U;
        if (param_length <= SU03T_LINK_MAX_PARAMS) {
            su03t_event_t event = {
                .msgno = frame[2],
                .param_length = param_length,
            };
            if (param_length > 0U) {
                memcpy(event.params, &frame[3], param_length);
            }
            if (s_event_callback != NULL) {
                s_event_callback(&event, s_event_context);
            }
        }
        su03t_parser_reset(length, in_frame);
    }
}

static void su03t_rx_task(void *arg)
{
    (void)arg;
    uint8_t buffer[128];
    uint8_t frame[SU03T_FRAME_MAX_SIZE];
    size_t frame_length = 0U;
    bool in_frame = false;

    while (true) {
        int received = uart_read_bytes(s_uart_num, buffer, sizeof(buffer),
                                       pdMS_TO_TICKS(100));
        for (int i = 0; i < received; ++i) {
            su03t_parser_feed(buffer[i], frame, &frame_length, &in_frame);
        }
    }
}

void su03t_link_set_event_callback(su03t_event_callback_t callback, void *context)
{
    s_event_callback = callback;
    s_event_context = context;
}

esp_err_t su03t_link_start(const su03t_link_config_t *config)
{
    if (s_started) {
        return ESP_ERR_INVALID_STATE;
    }

    su03t_link_config_t selected = {
        .uart_num = SU03T_LINK_DEFAULT_UART_NUM,
        .tx_gpio = SU03T_LINK_DEFAULT_TX_GPIO,
        .rx_gpio = SU03T_LINK_DEFAULT_RX_GPIO,
        .baud_rate = SU03T_LINK_DEFAULT_BAUD,
        .rx_buffer_size = 2048,
        .task_stack_size = 3072,
        .task_priority = 9,
    };
    if (config != NULL) {
        selected = *config;
    }

    if (selected.uart_num < UART_NUM_0 || selected.uart_num >= UART_NUM_MAX ||
        selected.baud_rate <= 0 || selected.rx_buffer_size < 256 ||
        selected.task_stack_size < 2048 || selected.task_priority < 1) {
        return ESP_ERR_INVALID_ARG;
    }

    uart_config_t uart_config = {
        .baud_rate = selected.baud_rate,
        .data_bits = UART_DATA_8_BITS,
        .parity = UART_PARITY_DISABLE,
        .stop_bits = UART_STOP_BITS_1,
        .flow_ctrl = UART_HW_FLOWCTRL_DISABLE,
        .source_clk = UART_SCLK_DEFAULT,
    };

    esp_err_t err = uart_param_config(selected.uart_num, &uart_config);
    if (err == ESP_OK) {
        err = uart_set_pin(selected.uart_num, selected.tx_gpio, selected.rx_gpio,
                           UART_PIN_NO_CHANGE, UART_PIN_NO_CHANGE);
    }
    if (err == ESP_OK) {
        err = uart_driver_install(selected.uart_num, selected.rx_buffer_size, 0, 0,
                                  NULL, 0);
    }
    if (err != ESP_OK) {
        return err;
    }

    s_uart_num = selected.uart_num;
    if (xTaskCreate(su03t_rx_task, "su03t_uart_rx", selected.task_stack_size, NULL,
                    selected.task_priority, NULL) != pdPASS) {
        uart_driver_delete(selected.uart_num);
        return ESP_ERR_NO_MEM;
    }

    s_started = true;
    ESP_LOGI(TAG, "SU-03T UART started: UART%d TX=%d RX=%d baud=%d",
             selected.uart_num, selected.tx_gpio, selected.rx_gpio, selected.baud_rate);
    return ESP_OK;
}
