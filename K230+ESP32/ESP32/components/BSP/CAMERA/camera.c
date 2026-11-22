#include "camera.h"

#include <stdbool.h>

#include "esp_log.h"

#include "camera_preview_config.h"

#define CAM_PIN_PWDN      (-1)
#define CAM_PIN_RESET     (-1)
#define CAM_PIN_XCLK      15
#define CAM_PIN_SIOD      4
#define CAM_PIN_SIOC      5
#define CAM_PIN_D0        11
#define CAM_PIN_D1        9
#define CAM_PIN_D2        8
#define CAM_PIN_D3        10
#define CAM_PIN_D4        12
#define CAM_PIN_D5        18
#define CAM_PIN_D6        17
#define CAM_PIN_D7        16
#define CAM_PIN_VSYNC     6
#define CAM_PIN_HREF      7
#define CAM_PIN_PCLK      13

static const char *TAG = "CAMERA";
static bool s_initialized;

static const camera_config_t s_camera_config = {
    .pin_pwdn = CAM_PIN_PWDN,
    .pin_reset = CAM_PIN_RESET,
    .pin_xclk = CAM_PIN_XCLK,
    .pin_sccb_sda = CAM_PIN_SIOD,
    .pin_sccb_scl = CAM_PIN_SIOC,
    .pin_d7 = CAM_PIN_D7,
    .pin_d6 = CAM_PIN_D6,
    .pin_d5 = CAM_PIN_D5,
    .pin_d4 = CAM_PIN_D4,
    .pin_d3 = CAM_PIN_D3,
    .pin_d2 = CAM_PIN_D2,
    .pin_d1 = CAM_PIN_D1,
    .pin_d0 = CAM_PIN_D0,
    .pin_vsync = CAM_PIN_VSYNC,
    .pin_href = CAM_PIN_HREF,
    .pin_pclk = CAM_PIN_PCLK,
    .xclk_freq_hz = CAMERA_XCLK_HZ,
    .ledc_timer = LEDC_TIMER_1,
    .ledc_channel = LEDC_CHANNEL_1,
    .pixel_format = PIXFORMAT_JPEG,
    .frame_size = FRAMESIZE_VGA,
    .jpeg_quality = 10,
    .fb_count = CAMERA_FRAME_BUFFER_COUNT,
    .fb_location = CAMERA_FB_IN_PSRAM,
    .grab_mode = CAMERA_USE_LATEST_FRAME ? CAMERA_GRAB_LATEST : CAMERA_GRAB_WHEN_EMPTY,
    .sccb_i2c_port = 1,
};

esp_err_t camera_init(void)
{
    if (s_initialized) {
        return ESP_ERR_INVALID_STATE;
    }

    esp_err_t err = esp_camera_init(&s_camera_config);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "OV2640 初始化失败: %s", esp_err_to_name(err));
        return err;
    }

    s_initialized = true;
    ESP_LOGI(TAG, "OV2640 初始化完成，JPEG/VGA，%d 个 PSRAM 帧缓冲", CAMERA_FRAME_BUFFER_COUNT);
    return ESP_OK;
}

esp_err_t camera_set_quality(int quality)
{
    if (!s_initialized) {
        return ESP_ERR_INVALID_STATE;
    }
    if (quality < 0 || quality > 63) {
        return ESP_ERR_INVALID_ARG;
    }

    sensor_t *sensor = esp_camera_sensor_get();
    if (sensor == NULL || sensor->set_quality == NULL) {
        return ESP_ERR_NOT_FOUND;
    }
    if (sensor->set_quality(sensor, quality) != 0) {
        return ESP_FAIL;
    }

    ESP_LOGI(TAG, "JPEG 画质设置为 %d", quality);
    return ESP_OK;
}

camera_fb_t *camera_get_frame(void)
{
    if (!s_initialized) {
        return NULL;
    }
    return esp_camera_fb_get();
}

void camera_return_frame(camera_fb_t *frame)
{
    if (frame != NULL) {
        esp_camera_fb_return(frame);
    }
}

esp_err_t camera_deinit(void)
{
    if (!s_initialized) {
        return ESP_ERR_INVALID_STATE;
    }

    esp_err_t err = esp_camera_deinit();
    if (err == ESP_OK) {
        s_initialized = false;
    }
    return err;
}
