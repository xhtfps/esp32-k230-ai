#pragma once

#include <stddef.h>
#include <stdint.h>

#include "esp_err.h"

#define ASSISTANT_STATE_PAYLOAD_SIZE 4U

typedef enum {
    ASSISTANT_STATE_IDLE = 0,
    ASSISTANT_STATE_LISTENING,
    ASSISTANT_STATE_TRANSCRIBING,
    ASSISTANT_STATE_THINKING,
    ASSISTANT_STATE_SPEAKING,
    ASSISTANT_STATE_ERROR,
} assistant_state_t;

typedef enum {
    ASSISTANT_UI_ACTION_NONE = 0,
    ASSISTANT_UI_ACTION_START_LISTENING = 1,
    ASSISTANT_UI_ACTION_STOP_LISTENING = 2,
    ASSISTANT_UI_ACTION_STOP_PLAYBACK = 3,
    ASSISTANT_UI_ACTION_ANALYZE_VISION = 4,
} assistant_ui_action_t;

esp_err_t assistant_codec_build_state(assistant_state_t state, uint16_t session_id,
                                      uint8_t error_code, uint8_t *payload,
                                      size_t payload_size);
esp_err_t assistant_codec_decode_ui_action(const uint8_t *payload, size_t payload_size,
                                           assistant_ui_action_t *action);
