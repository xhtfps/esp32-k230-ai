#include <assert.h>
#include <stdint.h>
#include <string.h>

#include "k230_protocol.h"

static void test_encode_and_decode(void)
{
    static const uint8_t payload[] = "{\"task\":\"face\",\"count\":0}";
    uint8_t encoded[K230_PROTOCOL_MAX_FRAME_SIZE];
    size_t encoded_length = 0;
    assert(k230_protocol_encode(K230_FRAME_RESULT, 7, payload, sizeof(payload) - 1,
                                 encoded, sizeof(encoded), &encoded_length) == ESP_OK);

    uint8_t storage[K230_PROTOCOL_MAX_PAYLOAD];
    k230_protocol_decoder_t decoder;
    k230_protocol_decoder_init(&decoder, storage, sizeof(storage));
    k230_protocol_frame_t frame = {0};
    esp_err_t result = ESP_ERR_NOT_FINISHED;
    for (size_t i = 0; i < encoded_length; ++i) {
        result = k230_protocol_decoder_feed(&decoder, encoded[i], &frame);
    }
    assert(result == ESP_OK);
    assert(frame.type == K230_FRAME_RESULT);
    assert(frame.sequence == 7);
    assert(frame.payload_length == sizeof(payload) - 1);
    assert(memcmp(frame.payload, payload, frame.payload_length) == 0);
}

static void test_resync_and_crc(void)
{
    uint8_t encoded[K230_PROTOCOL_MAX_FRAME_SIZE];
    size_t encoded_length = 0;
    assert(k230_protocol_encode(K230_FRAME_HEARTBEAT, 0, NULL, 0,
                                encoded, sizeof(encoded), &encoded_length) == ESP_OK);

    uint8_t storage[32];
    k230_protocol_decoder_t decoder;
    k230_protocol_decoder_init(&decoder, storage, sizeof(storage));
    k230_protocol_frame_t frame = {0};
    assert(k230_protocol_decoder_feed(&decoder, 0x00, &frame) == ESP_ERR_NOT_FINISHED);
    esp_err_t result = ESP_ERR_NOT_FINISHED;
    for (size_t i = 0; i < encoded_length; ++i) {
        result = k230_protocol_decoder_feed(&decoder, encoded[i], &frame);
    }
    assert(result == ESP_OK);

    encoded[encoded_length - 1] ^= 0x01;
    k230_protocol_decoder_init(&decoder, storage, sizeof(storage));
    result = ESP_ERR_NOT_FINISHED;
    for (size_t i = 0; i < encoded_length; ++i) {
        result = k230_protocol_decoder_feed(&decoder, encoded[i], &frame);
    }
    assert(result == ESP_ERR_INVALID_CRC);
}

int main(void)
{
    test_encode_and_decode();
    test_resync_and_crc();
    return 0;
}
