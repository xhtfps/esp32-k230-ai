#pragma once

#include "esp_camera.h"
#include "esp_err.h"

esp_err_t camera_init(void);
esp_err_t camera_set_quality(int quality);
camera_fb_t *camera_get_frame(void);
void camera_return_frame(camera_fb_t *frame);
esp_err_t camera_deinit(void);
