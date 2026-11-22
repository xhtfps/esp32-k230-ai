#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "esp_err.h"

#define K230_PROTOCOL_MAGIC_0        0xA5U
#define K230_PROTOCOL_MAGIC_1        0x5AU
#define K230_PROTOCOL_VERSION        0x01U
#define K230_PROTOCOL_HEADER_SIZE    8U
#define K230_PROTOCOL_TRAILER_SIZE   2U
#define K230_PROTOCOL_MAX_PAYLOAD   UINT16_MAX
#define K230_PROTOCOL_MAX_FRAME_SIZE \
    (K230_PROTOCOL_HEADER_SIZE + K230_PROTOCOL_MAX_PAYLOAD + K230_PROTOCOL_TRAILER_SIZE)

#define K230_FRAME_RESULT             0x01U
#define K230_FRAME_HEARTBEAT          0x02U
#define K230_FRAME_REQUEST_JPEG       0x10U
#define K230_FRAME_JPEG_CHUNK         0x11U
#define K230_FRAME_JPEG_COMPLETE      0x12U
#define K230_FRAME_JPEG_ACK           0x13U
#define K230_FRAME_UI_ACTION          0x20U
#define K230_FRAME_ASSISTANT_STATE    0x21U
#define K230_FRAME_TRANSCRIPT         0x22U
#define K230_FRAME_ANSWER_CHUNK       0x23U
#define K230_FRAME_ASSISTANT_ERROR    0x24U
#define K230_FRAME_ERROR              0x7FU

typedef struct {
    uint8_t type;
    uint16_t sequence;
    const uint8_t *payload;
    uint16_t payload_length;
} k230_protocol_frame_t;

typedef struct {
    uint8_t *payload_storage;
    size_t payload_capacity;
    uint8_t state;
    uint8_t header[6];
    size_t header_length;
    uint8_t type;
    uint16_t sequence;
    uint16_t payload_length;
    size_t payload_received;
    uint8_t crc_bytes[2];
    uint8_t crc_received;
} k230_protocol_decoder_t;

uint16_t k230_protocol_crc16(const uint8_t *data, size_t length);

esp_err_t k230_protocol_encode(uint8_t type, uint16_t sequence,
                                const uint8_t *payload, uint16_t payload_length,
                                uint8_t *out, size_t out_size, size_t *written);

void k230_protocol_decoder_init(k230_protocol_decoder_t *decoder,
                                uint8_t *payload_storage, size_t payload_capacity);

/* Feed one byte. ESP_OK means a complete valid frame is available in `frame`. */
esp_err_t k230_protocol_decoder_feed(k230_protocol_decoder_t *decoder,
                                     uint8_t byte, k230_protocol_frame_t *frame);
