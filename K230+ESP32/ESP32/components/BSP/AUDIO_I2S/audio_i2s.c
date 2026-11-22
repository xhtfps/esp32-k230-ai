#include "audio_i2s.h"

#include <stdlib.h>

#include "driver/gpio.h"
#include "driver/i2s_std.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"

#define AUDIO_I2S_PORT       I2S_NUM_0
#define AUDIO_I2S_PIN_BCLK   GPIO_NUM_40
#define AUDIO_I2S_PIN_WS     GPIO_NUM_39
#define AUDIO_I2S_PIN_DIN    GPIO_NUM_38
#define AUDIO_I2S_PIN_DOUT   GPIO_NUM_16
#define AUDIO_I2S_DMA_FRAMES 320U

static const char *TAG = "AUDIO_I2S";
static i2s_chan_handle_t s_tx_channel;
static i2s_chan_handle_t s_rx_channel;
static int32_t *s_tx_scratch;
static bool s_initialized;

esp_err_t audio_i2s_init(void)
{
    if (s_initialized) {
        return ESP_ERR_INVALID_STATE;
    }

    i2s_chan_config_t channel_config = I2S_CHANNEL_DEFAULT_CONFIG(AUDIO_I2S_PORT, I2S_ROLE_MASTER);
    channel_config.dma_desc_num = 6;
    channel_config.dma_frame_num = AUDIO_I2S_DMA_FRAMES;
    esp_err_t err = i2s_new_channel(&channel_config, &s_tx_channel, &s_rx_channel);

    i2s_std_config_t std_config = {
        .clk_cfg = I2S_STD_CLK_DEFAULT_CONFIG(AUDIO_I2S_SAMPLE_RATE_HZ),
        .slot_cfg = I2S_STD_PHILIPS_SLOT_DEFAULT_CONFIG(I2S_DATA_BIT_WIDTH_32BIT,
                                                        I2S_SLOT_MODE_STEREO),
        .gpio_cfg = {
            .mclk = I2S_GPIO_UNUSED,
            .bclk = AUDIO_I2S_PIN_BCLK,
            .ws = AUDIO_I2S_PIN_WS,
            .dout = AUDIO_I2S_PIN_DOUT,
            .din = AUDIO_I2S_PIN_DIN,
            .invert_flags = {0},
        },
    };
    if (err == ESP_OK) {
        err = i2s_channel_init_std_mode(s_tx_channel, &std_config);
    }
    if (err == ESP_OK) {
        err = i2s_channel_init_std_mode(s_rx_channel, &std_config);
    }
    if (err == ESP_OK) {
        err = i2s_channel_enable(s_tx_channel);
    }
    if (err == ESP_OK) {
        err = i2s_channel_enable(s_rx_channel);
    }
    if (err == ESP_OK) {
        s_tx_scratch = calloc(AUDIO_I2S_DMA_FRAMES * 2U, sizeof(int32_t));
        if (s_tx_scratch == NULL) {
            err = ESP_ERR_NO_MEM;
        }
    }
    if (err != ESP_OK) {
        if (s_tx_channel != NULL) {
            i2s_channel_disable(s_tx_channel);
        }
        if (s_rx_channel != NULL) {
            i2s_channel_disable(s_rx_channel);
        }
        if (s_tx_channel != NULL) {
            i2s_del_channel(s_tx_channel);
            s_tx_channel = NULL;
        }
        if (s_rx_channel != NULL) {
            i2s_del_channel(s_rx_channel);
            s_rx_channel = NULL;
        }
        return err;
    }

    s_initialized = true;
    ESP_LOGI(TAG, "I2S ready: rate=%u BCLK=%d WS=%d DIN=%d DOUT=%d",
             AUDIO_I2S_SAMPLE_RATE_HZ, AUDIO_I2S_PIN_BCLK, AUDIO_I2S_PIN_WS,
             AUDIO_I2S_PIN_DIN, AUDIO_I2S_PIN_DOUT);
    return ESP_OK;
}

esp_err_t audio_i2s_read_raw(int32_t *samples, size_t sample_capacity,
                             size_t *samples_read, uint32_t timeout_ms)
{
    if (!s_initialized) {
        return ESP_ERR_INVALID_STATE;
    }
    if (samples == NULL || samples_read == NULL || sample_capacity == 0U) {
        return ESP_ERR_INVALID_ARG;
    }
    size_t bytes_read = 0;
    esp_err_t err = i2s_channel_read(s_rx_channel, samples,
                                     sample_capacity * sizeof(int32_t), &bytes_read,
                                     pdMS_TO_TICKS(timeout_ms));
    *samples_read = bytes_read / sizeof(int32_t);
    return err;
}

esp_err_t audio_i2s_read_pcm16(int16_t *samples, size_t sample_capacity,
                               size_t *samples_read, uint32_t timeout_ms)
{
    if (samples == NULL || samples_read == NULL || sample_capacity == 0U ||
        sample_capacity > AUDIO_I2S_DMA_FRAMES) {
        return ESP_ERR_INVALID_ARG;
    }
    int32_t raw[AUDIO_I2S_DMA_FRAMES * 2U];
    size_t raw_count = 0;
    esp_err_t err = audio_i2s_read_raw(raw, sample_capacity * 2U, &raw_count, timeout_ms);
    if (err != ESP_OK) {
        *samples_read = 0;
        return err;
    }

    const size_t frame_count = raw_count / 2U;
    for (size_t i = 0; i < frame_count; ++i) {
        samples[i] = (int16_t)(raw[i * 2U] >> 16);
    }
    *samples_read = frame_count;
    return ESP_OK;
}

esp_err_t audio_i2s_write_pcm16(const int16_t *samples, size_t sample_count,
                                size_t *samples_written, uint32_t timeout_ms)
{
    if (!s_initialized) {
        return ESP_ERR_INVALID_STATE;
    }
    if (samples == NULL || samples_written == NULL || sample_count == 0U ||
        sample_count > AUDIO_I2S_DMA_FRAMES) {
        return ESP_ERR_INVALID_ARG;
    }
    for (size_t i = 0; i < sample_count; ++i) {
        const int32_t expanded = (int32_t)samples[i] * 65536;
        s_tx_scratch[i * 2U] = expanded;
        s_tx_scratch[i * 2U + 1U] = expanded;
    }

    size_t bytes_written = 0;
    esp_err_t err = i2s_channel_write(s_tx_channel, s_tx_scratch,
                                      sample_count * 2U * sizeof(int32_t),
                                      &bytes_written, pdMS_TO_TICKS(timeout_ms));
    *samples_written = bytes_written / (2U * sizeof(int32_t));
    return err;
}

esp_err_t audio_i2s_play_test_tone(uint32_t duration_ms)
{
    if (!s_initialized) {
        return ESP_ERR_INVALID_STATE;
    }
    if (duration_ms == 0U || duration_ms > 5000U) {
        return ESP_ERR_INVALID_ARG;
    }

    int16_t mono[AUDIO_I2S_DMA_FRAMES];
    const size_t total_samples = (AUDIO_I2S_SAMPLE_RATE_HZ * duration_ms) / 1000U;
    size_t generated = 0;
    while (generated < total_samples) {
        size_t batch = total_samples - generated;
        if (batch > AUDIO_I2S_DMA_FRAMES) {
            batch = AUDIO_I2S_DMA_FRAMES;
        }
        for (size_t i = 0; i < batch; ++i) {
            const size_t phase = (generated + i) % (AUDIO_I2S_SAMPLE_RATE_HZ / 440U);
            mono[i] = phase < (AUDIO_I2S_SAMPLE_RATE_HZ / 880U) ? 1800 : -1800;
        }
        size_t written = 0;
        esp_err_t err = audio_i2s_write_pcm16(mono, batch, &written, 1000U);
        if (err != ESP_OK) {
            return err;
        }
        if (written == 0U) {
            return ESP_ERR_TIMEOUT;
        }
        generated += written;
    }
    return ESP_OK;
}
