#include "k230_link.h"

#include <string.h>

#include "driver/uart.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"

#include "k230_link_config.h"

static const char *TAG = "K230_LINK";

static bool s_started;
static int s_uart_num = K230_LINK_UART_NUM;
static int64_t s_last_rx_us;
static uint8_t *s_rx_payload;
static uint8_t *s_tx_frame;
static SemaphoreHandle_t s_tx_lock;
static k230_protocol_decoder_t s_decoder;
static k230_link_frame_callback_t s_frame_callback;
static void *s_frame_callback_context;
static uint16_t s_tx_sequence;

static void k230_link_rx_task(void *arg)
{
    (void)arg;
    uint8_t buffer[1024];
    k230_protocol_frame_t frame = {0};

    while (true) {
        int received = uart_read_bytes((uart_port_t)s_uart_num, buffer, sizeof(buffer),
                                       pdMS_TO_TICKS(100));
        for (int i = 0; i < received; ++i) {
            esp_err_t err = k230_protocol_decoder_feed(&s_decoder, buffer[i], &frame);
            if (err == ESP_OK) {
                s_last_rx_us = esp_timer_get_time();
                k230_link_frame_callback_t callback = s_frame_callback;
                if (callback != NULL) {
                    callback(&frame, s_frame_callback_context);
                }
            } else if (err != ESP_ERR_NOT_FINISHED && err != ESP_ERR_INVALID_CRC) {
                ESP_LOGW(TAG, "丢弃无效 UART 帧：%s", esp_err_to_name(err));
            }
        }
    }
}

static void k230_link_heartbeat_task(void *arg)
{
    (void)arg;
    while (true) {
        esp_err_t err = k230_link_send(K230_FRAME_HEARTBEAT, NULL, 0);
        if (err != ESP_OK && err != ESP_ERR_INVALID_STATE) {
            ESP_LOGW(TAG, "发送心跳失败：%s", esp_err_to_name(err));
        }
        vTaskDelay(pdMS_TO_TICKS(K230_LINK_HEARTBEAT_MS));
    }
}

static void k230_link_free_buffers(void)
{
    if (s_rx_payload != NULL) {
        heap_caps_free(s_rx_payload);
        s_rx_payload = NULL;
    }
    if (s_tx_frame != NULL) {
        heap_caps_free(s_tx_frame);
        s_tx_frame = NULL;
    }
}

esp_err_t k230_link_start(const k230_link_config_t *config)
{
    if (s_started) {
        return ESP_ERR_INVALID_STATE;
    }

    k230_link_config_t selected = {
        .uart_num = K230_LINK_UART_NUM,
        .tx_gpio = K230_LINK_TX_GPIO,
        .rx_gpio = K230_LINK_RX_GPIO,
        .baud_rate = K230_LINK_BAUD_RATE,
        .rx_buffer_size = K230_LINK_RX_BUFFER_SIZE,
        .tx_buffer_size = K230_LINK_TX_BUFFER_SIZE,
        .task_stack_size = K230_LINK_TASK_STACK_SIZE,
        .task_priority = K230_LINK_TASK_PRIORITY,
    };
    if (config != NULL) {
        selected = *config;
    }
    if (selected.uart_num < UART_NUM_0 || selected.uart_num >= UART_NUM_MAX ||
        selected.baud_rate <= 0 || selected.rx_buffer_size < 1024 ||
        selected.tx_buffer_size < 1024 || selected.task_stack_size < 2048 ||
        selected.task_priority < 1) {
        return ESP_ERR_INVALID_ARG;
    }

    s_rx_payload = heap_caps_malloc(K230_PROTOCOL_MAX_PAYLOAD,
                                     MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (s_rx_payload == NULL) {
        s_rx_payload = heap_caps_malloc(K230_PROTOCOL_MAX_PAYLOAD, MALLOC_CAP_8BIT);
    }
    s_tx_frame = heap_caps_malloc(K230_PROTOCOL_MAX_FRAME_SIZE,
                                  MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (s_tx_frame == NULL) {
        s_tx_frame = heap_caps_malloc(K230_PROTOCOL_MAX_FRAME_SIZE, MALLOC_CAP_8BIT);
    }
    s_tx_lock = xSemaphoreCreateMutex();
    if (s_rx_payload == NULL || s_tx_frame == NULL || s_tx_lock == NULL) {
        k230_link_free_buffers();
        if (s_tx_lock != NULL) {
            vSemaphoreDelete(s_tx_lock);
            s_tx_lock = NULL;
        }
        return ESP_ERR_NO_MEM;
    }

    uart_config_t uart_config = {
        .baud_rate = selected.baud_rate,
        .data_bits = UART_DATA_8_BITS,
        .parity = UART_PARITY_DISABLE,
        .stop_bits = UART_STOP_BITS_1,
        .flow_ctrl = UART_HW_FLOWCTRL_DISABLE,
        .source_clk = UART_SCLK_DEFAULT,
    };
    esp_err_t err = uart_param_config((uart_port_t)selected.uart_num, &uart_config);
    if (err == ESP_OK) {
        err = uart_set_pin((uart_port_t)selected.uart_num, selected.tx_gpio, selected.rx_gpio,
                           UART_PIN_NO_CHANGE, UART_PIN_NO_CHANGE);
    }
    if (err == ESP_OK) {
        err = uart_driver_install((uart_port_t)selected.uart_num, selected.rx_buffer_size,
                                  selected.tx_buffer_size, 20, NULL, 0);
    }
    if (err != ESP_OK) {
        k230_link_free_buffers();
        vSemaphoreDelete(s_tx_lock);
        s_tx_lock = NULL;
        return err;
    }

    s_uart_num = selected.uart_num;
    s_last_rx_us = 0;
    s_tx_sequence = 0;
    k230_protocol_decoder_init(&s_decoder, s_rx_payload, K230_PROTOCOL_MAX_PAYLOAD);
    s_started = true;
    if (xTaskCreate(k230_link_rx_task, "k230_uart_rx", selected.task_stack_size, NULL,
                    selected.task_priority, NULL) != pdPASS ||
        xTaskCreate(k230_link_heartbeat_task, "k230_heartbeat", 3072, NULL,
                    selected.task_priority, NULL) != pdPASS) {
        s_started = false;
        uart_driver_delete((uart_port_t)s_uart_num);
        k230_link_free_buffers();
        vSemaphoreDelete(s_tx_lock);
        s_tx_lock = NULL;
        return ESP_ERR_NO_MEM;
    }

    ESP_LOGI(TAG, "UART 链路已启动：UART%d TX=%d RX=%d baud=%d",
             selected.uart_num, selected.tx_gpio, selected.rx_gpio, selected.baud_rate);
    return ESP_OK;
}

esp_err_t k230_link_send(uint8_t type, const uint8_t *payload, size_t length)
{
    if (!s_started || s_tx_lock == NULL) {
        return ESP_ERR_INVALID_STATE;
    }
    if (length > K230_PROTOCOL_MAX_PAYLOAD || (length > 0U && payload == NULL)) {
        return ESP_ERR_INVALID_SIZE;
    }
    if (xSemaphoreTake(s_tx_lock, pdMS_TO_TICKS(1000)) != pdTRUE) {
        return ESP_ERR_TIMEOUT;
    }

    size_t frame_length = 0;
    esp_err_t err = k230_protocol_encode(type, s_tx_sequence++, payload, (uint16_t)length,
                                          s_tx_frame, K230_PROTOCOL_MAX_FRAME_SIZE,
                                          &frame_length);
    if (err == ESP_OK) {
        int written = uart_write_bytes((uart_port_t)s_uart_num, s_tx_frame, frame_length);
        if (written != (int)frame_length) {
            err = ESP_FAIL;
        }
    }
    xSemaphoreGive(s_tx_lock);
    return err;
}

esp_err_t k230_link_request_jpeg(uint16_t request_id)
{
    uint8_t payload[2] = {
        (uint8_t)(request_id & 0xFFU),
        (uint8_t)(request_id >> 8),
    };
    return k230_link_send(K230_FRAME_REQUEST_JPEG, payload, sizeof(payload));
}

void k230_link_set_frame_callback(k230_link_frame_callback_t callback, void *context)
{
    s_frame_callback = callback;
    s_frame_callback_context = context;
}

bool k230_link_is_online(void)
{
    return s_last_rx_us > 0 &&
           (esp_timer_get_time() - s_last_rx_us) <= (int64_t)K230_LINK_OFFLINE_MS * 1000;
}

int64_t k230_link_last_rx_us(void)
{
    return s_last_rx_us;
}
