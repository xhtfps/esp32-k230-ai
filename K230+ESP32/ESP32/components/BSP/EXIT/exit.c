#include "exit.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "esp_timer.h"

#include "wifi.h"
#include "exit_task_config.h"

#define EXIT_TASK_PRIORITY 5

static QueueHandle_t s_exit_event_queue;
static const char *TAG = "EXIT";

static void exit_task(void *arg)
{
    uint32_t gpio_num;

    while (1)
    {
        xQueueReceive(s_exit_event_queue, &gpio_num, portMAX_DELAY);

        vTaskDelay(pdMS_TO_TICKS(30));
        if (gpio_get_level(BOOT_GPIO_PIN) == 0)
        {
            int64_t pressed_at_us = esp_timer_get_time();

            while (gpio_get_level(BOOT_GPIO_PIN) == 0)
            {
                vTaskDelay(pdMS_TO_TICKS(10));
            }

            uint32_t held_ms = (uint32_t)((esp_timer_get_time() - pressed_at_us) / 1000);
            esp_err_t err;
            exit_press_type_t press_type = exit_classify_press(held_ms);
            if (exit_press_switches_mode(press_type)) {
                exit_wifi_mode_t next_mode = exit_next_mode(wifi_is_provisioning());
                if (next_mode == EXIT_WIFI_MODE_PROVISIONING) {
                    ESP_LOGI(TAG, "GPIO0 长按，切换到配网热点模式");
                    err = wifi_start_provisioning();
                } else {
                    ESP_LOGI(TAG, "GPIO0 长按，切换到 STA 模式");
                    err = wifi_start_station();
                }
            } else {
                ESP_LOGI(TAG, "GPIO0 短按，未切换 Wi-Fi 模式");
                err = ESP_OK;
            }

            if (err != ESP_OK) {
                ESP_LOGE(TAG, "Wi-Fi 模式切换失败：%s", esp_err_to_name(err));
            }
        }

        xQueueReset(s_exit_event_queue);
    }
}

void exit_init(void)
{
    gpio_config_t gpio_init_struct = {0};
    gpio_init_struct.intr_type = GPIO_INTR_NEGEDGE;
    gpio_init_struct.pin_bit_mask = 1ULL << BOOT_GPIO_PIN;
    gpio_init_struct.mode = GPIO_MODE_INPUT;
    gpio_init_struct.pull_up_en = GPIO_PULLUP_ENABLE;
    gpio_init_struct.pull_down_en = GPIO_PULLDOWN_DISABLE;

    gpio_config(&gpio_init_struct);

    s_exit_event_queue = xQueueCreate(1, sizeof(uint32_t));
    configASSERT(s_exit_event_queue != NULL);
    configASSERT(xTaskCreate(exit_task, "exit_task", EXIT_TASK_STACK_SIZE,
                             NULL, EXIT_TASK_PRIORITY, NULL) == pdPASS);

    gpio_install_isr_service(0);

    gpio_isr_handler_add(BOOT_GPIO_PIN, exit_isr_handler, BOOT_GPIO_PIN);
}

void exit_isr_handler(void* arg)
{
    uint32_t gpio_num = (uint32_t) arg;
    BaseType_t task_woken = pdFALSE;

    if(gpio_num == BOOT_GPIO_PIN)
    {
        xQueueSendFromISR(s_exit_event_queue, &gpio_num, &task_woken);
        if (task_woken == pdTRUE)
        {
            portYIELD_FROM_ISR();
        }
    }
}
