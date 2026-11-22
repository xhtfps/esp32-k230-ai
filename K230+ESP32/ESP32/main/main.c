#include "assistant_codec.h"
#include "assistant_state.h"
#include "audio_i2s.h"
#include "exit.h"
#include "face_display.h"
#include "k230_link.h"
#include "led.h"
#include "vision_state.h"
#include "web.h"
#include "wifi.h"

#include "esp_log.h"
#include "esp_system.h"
#include "sdkconfig.h"

static const char *TAG = "MAIN";

#if CONFIG_ASSISTANT_PERIPHERAL_SELF_TEST
static void run_audio_self_test(void)
{
    esp_err_t err = audio_i2s_play_test_tone(250U);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "MAX98357 test tone failed: %s", esp_err_to_name(err));
    } else {
        ESP_LOGI(TAG, "MAX98357 test tone complete");
    }

    int16_t samples[320];
    size_t samples_read = 0;
    err = audio_i2s_read_pcm16(samples, 320U, &samples_read, 1000U);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "INMP441 capture test failed: %s", esp_err_to_name(err));
        return;
    }
    int32_t peak = 0;
    for (size_t i = 0; i < samples_read; ++i) {
        int32_t magnitude = samples[i];
        if (magnitude < 0) {
            magnitude = -magnitude;
        }
        if (magnitude > peak) {
            peak = magnitude;
        }
    }
    ESP_LOGI(TAG, "INMP441 capture test: samples=%u peak=%ld",
             (unsigned)samples_read, (long)peak);
}
#endif

static void handle_ui_action(const k230_protocol_frame_t *frame)
{
    assistant_ui_action_t action = ASSISTANT_UI_ACTION_NONE;
    esp_err_t err = assistant_codec_decode_ui_action(frame->payload,
                                                     frame->payload_length, &action);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "Invalid K230 UI action: %s", esp_err_to_name(err));
        return;
    }

    switch (action) {
    case ASSISTANT_UI_ACTION_START_LISTENING:
        err = assistant_state_set(ASSISTANT_STATE_LISTENING, 0);
        break;
    case ASSISTANT_UI_ACTION_STOP_LISTENING:
        err = assistant_state_set(ASSISTANT_STATE_TRANSCRIBING, 0);
        break;
    case ASSISTANT_UI_ACTION_STOP_PLAYBACK:
        err = assistant_state_set(ASSISTANT_STATE_IDLE, 0);
        break;
    case ASSISTANT_UI_ACTION_ANALYZE_VISION: {
        static uint16_t request_id;
        err = vision_state_prepare_jpeg(++request_id);
        if (err == ESP_OK) {
            err = k230_link_request_jpeg(request_id);
        }
        break;
    }
    default:
        err = ESP_ERR_NOT_SUPPORTED;
        break;
    }

    if (err != ESP_OK) {
        ESP_LOGW(TAG, "K230 UI action %u failed: %s", (unsigned)action,
                 esp_err_to_name(err));
    }
}

static void k230_frame_handler(const k230_protocol_frame_t *frame, void *context)
{
    (void)context;
    if (frame == NULL) {
        return;
    }
    if (frame->type == K230_FRAME_UI_ACTION) {
        handle_ui_action(frame);
        return;
    }

    esp_err_t err = vision_state_process_frame(frame);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "K230 frame failed: type=0x%02x error=%s",
                 frame->type, esp_err_to_name(err));
        return;
    }

    if (frame->type == K230_FRAME_ERROR) {
        ESP_LOGW(TAG, "K230 reported capture error (request=%u)",
                 frame->payload_length >= 2U
                     ? (unsigned)(frame->payload[0] | ((uint16_t)frame->payload[1] << 8))
                     : 0U);
    } else if (frame->type == K230_FRAME_JPEG_COMPLETE && frame->payload_length >= 2U) {
        err = k230_link_send(K230_FRAME_JPEG_ACK, frame->payload, 2U);
        if (err != ESP_OK) {
            ESP_LOGW(TAG, "JPEG ACK failed: %s", esp_err_to_name(err));
        }
    }
}

void app_main(void)
{
    ESP_LOGI(TAG, "Reset reason=%d free_heap=%u", (int)esp_reset_reason(),
             (unsigned)esp_get_free_heap_size());

    led_init();
    ESP_ERROR_CHECK(face_display_init());
    ESP_ERROR_CHECK(audio_i2s_init());
    ESP_ERROR_CHECK(assistant_state_init());
#if CONFIG_ASSISTANT_PERIPHERAL_SELF_TEST
    run_audio_self_test();
#endif

    ESP_ERROR_CHECK(wifi_init_sta());
    ESP_ERROR_CHECK(vision_state_init());

    k230_link_set_frame_callback(k230_frame_handler, NULL);
    ESP_ERROR_CHECK(k230_link_start(NULL));
    ESP_ERROR_CHECK(assistant_state_set(ASSISTANT_STATE_IDLE, 0));

    ESP_ERROR_CHECK(web_start());
    exit_init();

    ESP_LOGI(TAG, "Assistant peripherals ready; ASRPRO/OV2640/SD are disabled");
}
