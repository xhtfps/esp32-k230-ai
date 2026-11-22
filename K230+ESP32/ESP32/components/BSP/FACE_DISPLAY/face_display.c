#include "face_display.h"

#include <stdlib.h>
#include <string.h>

#include "driver/gpio.h"
#include "driver/spi_master.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"

#define FACE_LCD_HOST          SPI2_HOST
#define FACE_LCD_PIN_SCLK      GPIO_NUM_41
#define FACE_LCD_PIN_MOSI      GPIO_NUM_42
#define FACE_LCD_PIN_CS        GPIO_NUM_47
#define FACE_LCD_PIN_DC        GPIO_NUM_14
#define FACE_LCD_PIN_RST       GPIO_NUM_15
#define FACE_LCD_WIDTH         128U
#define FACE_LCD_HEIGHT        160U
#define FACE_LCD_SPI_HZ        (10 * 1000 * 1000)
#define FACE_LCD_X_OFFSET      0U
#define FACE_LCD_Y_OFFSET      0U

#define RGB565(r, g, b) \
    (uint16_t)((((uint16_t)(r) & 0xF8U) << 8) | (((uint16_t)(g) & 0xFCU) << 3) | ((uint16_t)(b) >> 3))

static const char *TAG = "FACE_DISPLAY";
static spi_device_handle_t s_lcd;
static SemaphoreHandle_t s_lock;
static uint16_t *s_line_buffer;
static bool s_initialized;

static esp_err_t lcd_write(bool data_mode, const void *data, size_t length)
{
    if (data == NULL || length == 0U) {
        return ESP_ERR_INVALID_ARG;
    }
    gpio_set_level(FACE_LCD_PIN_DC, data_mode ? 1 : 0);
    spi_transaction_t transaction = {
        .length = length * 8U,
        .tx_buffer = data,
    };
    return spi_device_polling_transmit(s_lcd, &transaction);
}

static esp_err_t lcd_command(uint8_t command)
{
    return lcd_write(false, &command, sizeof(command));
}

static esp_err_t lcd_data(const void *data, size_t length)
{
    return lcd_write(true, data, length);
}

static esp_err_t lcd_command_data(uint8_t command, const uint8_t *data, size_t length)
{
    esp_err_t err = lcd_command(command);
    if (err == ESP_OK && length > 0U) {
        err = lcd_data(data, length);
    }
    return err;
}

static esp_err_t lcd_set_window(uint16_t x, uint16_t y, uint16_t width, uint16_t height)
{
    const uint16_t x1 = (uint16_t)(x + FACE_LCD_X_OFFSET);
    const uint16_t x2 = (uint16_t)(x + width - 1U + FACE_LCD_X_OFFSET);
    const uint16_t y1 = (uint16_t)(y + FACE_LCD_Y_OFFSET);
    const uint16_t y2 = (uint16_t)(y + height - 1U + FACE_LCD_Y_OFFSET);
    const uint8_t column[] = {(uint8_t)(x1 >> 8), (uint8_t)x1, (uint8_t)(x2 >> 8), (uint8_t)x2};
    const uint8_t row[] = {(uint8_t)(y1 >> 8), (uint8_t)y1, (uint8_t)(y2 >> 8), (uint8_t)y2};

    esp_err_t err = lcd_command_data(0x2AU, column, sizeof(column));
    if (err == ESP_OK) {
        err = lcd_command_data(0x2BU, row, sizeof(row));
    }
    if (err == ESP_OK) {
        err = lcd_command(0x2CU);
    }
    return err;
}

static esp_err_t lcd_fill_rect(uint16_t x, uint16_t y, uint16_t width, uint16_t height,
                               uint16_t color)
{
    if (width == 0U || height == 0U || x >= FACE_LCD_WIDTH || y >= FACE_LCD_HEIGHT ||
        (uint32_t)x + width > FACE_LCD_WIDTH || (uint32_t)y + height > FACE_LCD_HEIGHT) {
        return ESP_ERR_INVALID_ARG;
    }

    esp_err_t err = lcd_set_window(x, y, width, height);
    if (err != ESP_OK) {
        return err;
    }
    const uint16_t wire_color = (uint16_t)((color << 8) | (color >> 8));
    for (uint16_t i = 0; i < width; ++i) {
        s_line_buffer[i] = wire_color;
    }
    for (uint16_t row = 0; row < height && err == ESP_OK; ++row) {
        err = lcd_data(s_line_buffer, width * sizeof(uint16_t));
    }
    return err;
}

static esp_err_t lcd_initialize_controller(void)
{
    gpio_set_level(FACE_LCD_PIN_RST, 0);
    vTaskDelay(pdMS_TO_TICKS(20));
    gpio_set_level(FACE_LCD_PIN_RST, 1);
    vTaskDelay(pdMS_TO_TICKS(120));

    esp_err_t err = lcd_command(0x01U);
    vTaskDelay(pdMS_TO_TICKS(150));
    if (err == ESP_OK) {
        err = lcd_command(0x11U);
    }
    vTaskDelay(pdMS_TO_TICKS(120));

    const uint8_t frame_rate[] = {0x01U, 0x2CU, 0x2DU};
    const uint8_t frame_rate_idle[] = {0x01U, 0x2CU, 0x2DU};
    const uint8_t frame_rate_partial[] = {0x01U, 0x2CU, 0x2DU, 0x01U, 0x2CU, 0x2DU};
    const uint8_t inversion = 0x07U;
    const uint8_t power_1[] = {0xA2U, 0x02U, 0x84U};
    const uint8_t power_2 = 0xC5U;
    const uint8_t power_3[] = {0x0AU, 0x00U};
    const uint8_t power_4[] = {0x8AU, 0x2AU};
    const uint8_t power_5[] = {0x8AU, 0xEEU};
    const uint8_t vcom = 0x0EU;
    const uint8_t gamma_pos[] = {
        0x02U, 0x1CU, 0x07U, 0x12U, 0x37U, 0x32U, 0x29U, 0x2DU,
        0x29U, 0x25U, 0x2BU, 0x39U, 0x00U, 0x01U, 0x03U, 0x10U,
    };
    const uint8_t gamma_neg[] = {
        0x03U, 0x1DU, 0x07U, 0x06U, 0x2EU, 0x2CU, 0x29U, 0x2DU,
        0x2EU, 0x2EU, 0x37U, 0x3FU, 0x00U, 0x00U, 0x02U, 0x10U,
    };
    if (err == ESP_OK) err = lcd_command_data(0xB1U, frame_rate, sizeof(frame_rate));
    if (err == ESP_OK) err = lcd_command_data(0xB2U, frame_rate_idle, sizeof(frame_rate_idle));
    if (err == ESP_OK) err = lcd_command_data(0xB3U, frame_rate_partial, sizeof(frame_rate_partial));
    if (err == ESP_OK) err = lcd_command_data(0xB4U, &inversion, sizeof(inversion));
    if (err == ESP_OK) err = lcd_command_data(0xC0U, power_1, sizeof(power_1));
    if (err == ESP_OK) err = lcd_command_data(0xC1U, &power_2, sizeof(power_2));
    if (err == ESP_OK) err = lcd_command_data(0xC2U, power_3, sizeof(power_3));
    if (err == ESP_OK) err = lcd_command_data(0xC3U, power_4, sizeof(power_4));
    if (err == ESP_OK) err = lcd_command_data(0xC4U, power_5, sizeof(power_5));
    if (err == ESP_OK) err = lcd_command_data(0xC5U, &vcom, sizeof(vcom));
    if (err == ESP_OK) err = lcd_command_data(0xE0U, gamma_pos, sizeof(gamma_pos));
    if (err == ESP_OK) err = lcd_command_data(0xE1U, gamma_neg, sizeof(gamma_neg));

    const uint8_t pixel_format = 0x05U;
    if (err == ESP_OK) {
        err = lcd_command_data(0x3AU, &pixel_format, sizeof(pixel_format));
    }
    // MX + MY relative to the previous orientation are cleared: rotate the panel 180 degrees.
    const uint8_t madctl = 0x08U;
    if (err == ESP_OK) {
        err = lcd_command_data(0x36U, &madctl, sizeof(madctl));
    }
    if (err == ESP_OK) {
        err = lcd_command(0x21U);
    }
    if (err == ESP_OK) {
        err = lcd_command(0x29U);
    }
    vTaskDelay(pdMS_TO_TICKS(20));
    return err;
}

esp_err_t face_display_init(void)
{
    if (s_initialized) {
        return ESP_ERR_INVALID_STATE;
    }
    gpio_config_t output_config = {
        .pin_bit_mask = (1ULL << FACE_LCD_PIN_DC) | (1ULL << FACE_LCD_PIN_RST),
        .mode = GPIO_MODE_OUTPUT,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    esp_err_t err = gpio_config(&output_config);

    spi_bus_config_t bus_config = {
        .mosi_io_num = FACE_LCD_PIN_MOSI,
        .miso_io_num = GPIO_NUM_NC,
        .sclk_io_num = FACE_LCD_PIN_SCLK,
        .quadwp_io_num = GPIO_NUM_NC,
        .quadhd_io_num = GPIO_NUM_NC,
        .max_transfer_sz = FACE_LCD_WIDTH * sizeof(uint16_t),
    };
    if (err == ESP_OK) {
        err = spi_bus_initialize(FACE_LCD_HOST, &bus_config, SPI_DMA_CH_AUTO);
    }
    spi_device_interface_config_t device_config = {
        .clock_speed_hz = FACE_LCD_SPI_HZ,
        .mode = 0,
        .spics_io_num = FACE_LCD_PIN_CS,
        .queue_size = 1,
    };
    if (err == ESP_OK) {
        err = spi_bus_add_device(FACE_LCD_HOST, &device_config, &s_lcd);
    }
    if (err == ESP_OK) {
        s_line_buffer = heap_caps_malloc(FACE_LCD_WIDTH * sizeof(uint16_t),
                                         MALLOC_CAP_DMA | MALLOC_CAP_INTERNAL);
        s_lock = xSemaphoreCreateMutex();
        if (s_line_buffer == NULL || s_lock == NULL) {
            err = ESP_ERR_NO_MEM;
        }
    }
    if (err == ESP_OK) {
        err = lcd_initialize_controller();
    }
    if (err != ESP_OK) {
        if (s_lock != NULL) {
            vSemaphoreDelete(s_lock);
            s_lock = NULL;
        }
        free(s_line_buffer);
        s_line_buffer = NULL;
        if (s_lcd != NULL) {
            spi_bus_remove_device(s_lcd);
            s_lcd = NULL;
        }
        spi_bus_free(FACE_LCD_HOST);
        return err;
    }

    s_initialized = true;
    ESP_LOGI(TAG, "ST7735 ready: %ux%u SCLK=%d MOSI=%d CS=%d DC=%d RST=%d",
             FACE_LCD_WIDTH, FACE_LCD_HEIGHT, FACE_LCD_PIN_SCLK, FACE_LCD_PIN_MOSI,
             FACE_LCD_PIN_CS, FACE_LCD_PIN_DC, FACE_LCD_PIN_RST);
    return face_display_set_expression(FACE_EXPRESSION_IDLE);
}

esp_err_t face_display_set_expression(face_expression_t expression)
{
    if (!s_initialized || s_lock == NULL) {
        return ESP_ERR_INVALID_STATE;
    }
    if (expression < FACE_EXPRESSION_IDLE || expression > FACE_EXPRESSION_ERROR) {
        return ESP_ERR_INVALID_ARG;
    }
    if (xSemaphoreTake(s_lock, pdMS_TO_TICKS(1000)) != pdTRUE) {
        return ESP_ERR_TIMEOUT;
    }

    uint16_t background = RGB565(8, 16, 36);
    uint16_t accent = RGB565(80, 220, 255);
    if (expression == FACE_EXPRESSION_LISTENING) {
        background = RGB565(5, 42, 40);
        accent = RGB565(70, 255, 175);
    } else if (expression == FACE_EXPRESSION_THINKING) {
        background = RGB565(30, 12, 56);
        accent = RGB565(190, 120, 255);
    } else if (expression == FACE_EXPRESSION_SPEAKING) {
        background = RGB565(50, 24, 4);
        accent = RGB565(255, 180, 70);
    } else if (expression == FACE_EXPRESSION_ERROR) {
        background = RGB565(55, 4, 8);
        accent = RGB565(255, 80, 90);
    }

    esp_err_t err = lcd_fill_rect(0, 0, FACE_LCD_WIDTH, FACE_LCD_HEIGHT, background);
    if (err == ESP_OK) {
        err = lcd_fill_rect(24, 48, 26, 16, accent);
    }
    if (err == ESP_OK) {
        err = lcd_fill_rect(78, 48, 26, 16, accent);
    }
    if (err == ESP_OK && expression == FACE_EXPRESSION_SPEAKING) {
        err = lcd_fill_rect(44, 94, 40, 30, accent);
    } else if (err == ESP_OK && expression == FACE_EXPRESSION_ERROR) {
        err = lcd_fill_rect(42, 108, 44, 7, accent);
    } else if (err == ESP_OK) {
        err = lcd_fill_rect(40, 100, 48, 10, accent);
    }

    xSemaphoreGive(s_lock);
    return err;
}
