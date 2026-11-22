#include <string.h>

#include "assistant_codec.h"
#include "unity.h"

TEST_CASE("assistant state payload is little endian", "[assistant]")
{
    uint8_t payload[ASSISTANT_STATE_PAYLOAD_SIZE] = {0};

    TEST_ASSERT_EQUAL(ESP_OK,
                      assistant_codec_build_state(ASSISTANT_STATE_THINKING, 0x1234U,
                                                  0x56U, payload, sizeof(payload)));
    const uint8_t expected[] = {ASSISTANT_STATE_THINKING, 0x34U, 0x12U, 0x56U};
    TEST_ASSERT_EQUAL_UINT8_ARRAY(expected, payload, sizeof(expected));
}

TEST_CASE("assistant state payload rejects invalid state", "[assistant]")
{
    uint8_t payload[ASSISTANT_STATE_PAYLOAD_SIZE] = {0};

    TEST_ASSERT_EQUAL(ESP_ERR_INVALID_ARG,
                      assistant_codec_build_state((assistant_state_t)99, 1U, 0U,
                                                  payload, sizeof(payload)));
}

TEST_CASE("ui action decoder accepts only known actions", "[assistant]")
{
    assistant_ui_action_t action = ASSISTANT_UI_ACTION_NONE;
    const uint8_t start[] = {ASSISTANT_UI_ACTION_START_LISTENING};
    const uint8_t invalid[] = {0xFFU};

    TEST_ASSERT_EQUAL(ESP_OK, assistant_codec_decode_ui_action(start, sizeof(start), &action));
    TEST_ASSERT_EQUAL(ASSISTANT_UI_ACTION_START_LISTENING, action);
    TEST_ASSERT_EQUAL(ESP_ERR_NOT_SUPPORTED,
                      assistant_codec_decode_ui_action(invalid, sizeof(invalid), &action));
}
