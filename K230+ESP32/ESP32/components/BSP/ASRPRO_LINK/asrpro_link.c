#include "asrpro_link.h"

#include <stdbool.h>

#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static const char *TAG = "ASRPRO_LINK";

static bool s_started;
static uart_port_t s_uart_num = ASRPRO_LINK_DEFAULT_UART_NUM;
static asrpro_event_callback_t s_event_callback;
static void *s_event_context;

static void asrpro_rx_task(void *arg)
{
    (void)arg;
    uint8_t buffer[64];

    while (true) {
        int received = uart_read_bytes(s_uart_num, buffer, sizeof(buffer),
                                       pdMS_TO_TICKS(100));
        for (int i = 0; i < received; ++i) {
            asrpro_event_t event = {.command_id = buffer[i]};
            ESP_LOGI(TAG, "ASRPRO command id=0x%02x", event.command_id);
            if (s_event_callback != NULL) {
                s_event_callback(&event, s_event_context);
            }
        }
    }
}

void asrpro_link_set_event_callback(asrpro_event_callback_t callback, void *context)
{
    s_event_callback = callback;
    s_event_context = context;
}

esp_err_t asrpro_link_start(const asrpro_link_config_t *config)
{
    if (s_started) {
        return ESP_ERR_INVALID_STATE;
    }

    asrpro_link_config_t selected = {
        .uart_num = ASRPRO_LINK_DEFAULT_UART_NUM,
        .tx_gpio = ASRPRO_LINK_DEFAULT_TX_GPIO,
        .rx_gpio = ASRPRO_LINK_DEFAULT_RX_GPIO,
        .baud_rate = ASRPRO_LINK_DEFAULT_BAUD,
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
    if (xTaskCreate(asrpro_rx_task, "asrpro_uart_rx", selected.task_stack_size, NULL,
                    selected.task_priority, NULL) != pdPASS) {
        uart_driver_delete(selected.uart_num);
        return ESP_ERR_NO_MEM;
    }

    s_started = true;
    ESP_LOGI(TAG, "ASRPRO UART started: UART%d TX=%d RX=%d baud=%d",
             selected.uart_num, selected.tx_gpio, selected.rx_gpio, selected.baud_rate);
    return ESP_OK;
}
