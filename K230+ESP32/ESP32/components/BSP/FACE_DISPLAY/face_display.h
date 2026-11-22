#pragma once

#include "esp_err.h"

typedef enum {
    FACE_EXPRESSION_IDLE = 0,
    FACE_EXPRESSION_LISTENING,
    FACE_EXPRESSION_THINKING,
    FACE_EXPRESSION_SPEAKING,
    FACE_EXPRESSION_ERROR,
} face_expression_t;

esp_err_t face_display_init(void);
esp_err_t face_display_set_expression(face_expression_t expression);
