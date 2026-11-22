#pragma once

#include <stdint.h>

#include "assistant_codec.h"
#include "esp_err.h"

esp_err_t assistant_state_init(void);
esp_err_t assistant_state_set(assistant_state_t state, uint8_t error_code);
assistant_state_t assistant_state_get(void);
esp_err_t assistant_state_publish_text(uint8_t frame_type, const char *text);
