#include "assistant_state.h"

#include <string.h>

#include "face_display.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "k230_link.h"
#include "k230_protocol.h"

static SemaphoreHandle_t s_state_lock;
static assistant_state_t s_state = ASSISTANT_STATE_IDLE;
static uint16_t s_session_id;
static bool s_initialized;

static face_expression_t face_for_state(assistant_state_t state)
{
    switch (state) {
    case ASSISTANT_STATE_LISTENING:
        return FACE_EXPRESSION_LISTENING;
    case ASSISTANT_STATE_TRANSCRIBING:
    case ASSISTANT_STATE_THINKING:
        return FACE_EXPRESSION_THINKING;
    case ASSISTANT_STATE_SPEAKING:
        return FACE_EXPRESSION_SPEAKING;
    case ASSISTANT_STATE_ERROR:
        return FACE_EXPRESSION_ERROR;
    case ASSISTANT_STATE_IDLE:
    default:
        return FACE_EXPRESSION_IDLE;
    }
}

esp_err_t assistant_state_init(void)
{
    if (s_initialized) {
        return ESP_ERR_INVALID_STATE;
    }
    s_state_lock = xSemaphoreCreateMutex();
    if (s_state_lock == NULL) {
        return ESP_ERR_NO_MEM;
    }
    s_state = ASSISTANT_STATE_IDLE;
    s_session_id = 0;
    s_initialized = true;
    return ESP_OK;
}

esp_err_t assistant_state_set(assistant_state_t state, uint8_t error_code)
{
    if (!s_initialized || s_state_lock == NULL) {
        return ESP_ERR_INVALID_STATE;
    }
    if (state < ASSISTANT_STATE_IDLE || state > ASSISTANT_STATE_ERROR) {
        return ESP_ERR_INVALID_ARG;
    }
    if (xSemaphoreTake(s_state_lock, pdMS_TO_TICKS(1000)) != pdTRUE) {
        return ESP_ERR_TIMEOUT;
    }

    if (state == ASSISTANT_STATE_LISTENING && s_state != ASSISTANT_STATE_LISTENING) {
        ++s_session_id;
    }
    s_state = state;
    const uint16_t session_id = s_session_id;
    xSemaphoreGive(s_state_lock);

    esp_err_t err = face_display_set_expression(face_for_state(state));
    uint8_t payload[ASSISTANT_STATE_PAYLOAD_SIZE];
    esp_err_t codec_err = assistant_codec_build_state(state, session_id, error_code,
                                                       payload, sizeof(payload));
    if (err == ESP_OK) {
        err = codec_err;
    }
    if (codec_err == ESP_OK) {
        esp_err_t link_err = k230_link_send(K230_FRAME_ASSISTANT_STATE,
                                            payload, sizeof(payload));
        if (err == ESP_OK) {
            err = link_err;
        }
    }
    return err;
}

assistant_state_t assistant_state_get(void)
{
    if (!s_initialized || s_state_lock == NULL ||
        xSemaphoreTake(s_state_lock, pdMS_TO_TICKS(100)) != pdTRUE) {
        return ASSISTANT_STATE_ERROR;
    }
    const assistant_state_t state = s_state;
    xSemaphoreGive(s_state_lock);
    return state;
}

esp_err_t assistant_state_publish_text(uint8_t frame_type, const char *text)
{
    if (!s_initialized) {
        return ESP_ERR_INVALID_STATE;
    }
    if ((frame_type != K230_FRAME_TRANSCRIPT && frame_type != K230_FRAME_ANSWER_CHUNK &&
         frame_type != K230_FRAME_ASSISTANT_ERROR) || text == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    const size_t length = strlen(text);
    if (length == 0U || length > K230_PROTOCOL_MAX_PAYLOAD) {
        return ESP_ERR_INVALID_SIZE;
    }
    return k230_link_send(frame_type, (const uint8_t *)text, length);
}
