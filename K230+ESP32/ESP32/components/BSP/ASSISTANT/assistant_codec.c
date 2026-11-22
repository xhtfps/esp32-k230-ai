#include "assistant_codec.h"

esp_err_t assistant_codec_build_state(assistant_state_t state, uint16_t session_id,
                                      uint8_t error_code, uint8_t *payload,
                                      size_t payload_size)
{
    if (state < ASSISTANT_STATE_IDLE || state > ASSISTANT_STATE_ERROR || payload == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    if (payload_size < ASSISTANT_STATE_PAYLOAD_SIZE) {
        return ESP_ERR_INVALID_SIZE;
    }

    payload[0] = (uint8_t)state;
    payload[1] = (uint8_t)(session_id & 0xFFU);
    payload[2] = (uint8_t)(session_id >> 8);
    payload[3] = error_code;
    return ESP_OK;
}

esp_err_t assistant_codec_decode_ui_action(const uint8_t *payload, size_t payload_size,
                                           assistant_ui_action_t *action)
{
    if (payload == NULL || action == NULL || payload_size != 1U) {
        return ESP_ERR_INVALID_ARG;
    }
    if (payload[0] < ASSISTANT_UI_ACTION_START_LISTENING ||
        payload[0] > ASSISTANT_UI_ACTION_ANALYZE_VISION) {
        return ESP_ERR_NOT_SUPPORTED;
    }

    *action = (assistant_ui_action_t)payload[0];
    return ESP_OK;
}
