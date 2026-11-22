#include "k230_protocol.h"

#include <string.h>

#define DECODER_SYNC_MAGIC_0 0U
#define DECODER_SYNC_MAGIC_1 1U
#define DECODER_HEADER       2U
#define DECODER_PAYLOAD      3U
#define DECODER_CRC          4U

uint16_t k230_protocol_crc16(const uint8_t *data, size_t length)
{
    uint16_t crc = 0xFFFFU;
    for (size_t i = 0; i < length; ++i) {
        crc ^= (uint16_t)data[i] << 8;
        for (unsigned bit = 0; bit < 8; ++bit) {
            crc = (crc & 0x8000U) != 0U ? (uint16_t)((crc << 1) ^ 0x1021U)
                                         : (uint16_t)(crc << 1);
        }
    }
    return crc;
}

static void decoder_reset(k230_protocol_decoder_t *decoder)
{
    decoder->state = DECODER_SYNC_MAGIC_0;
    decoder->header_length = 0;
    decoder->type = 0;
    decoder->sequence = 0;
    decoder->payload_length = 0;
    decoder->payload_received = 0;
    decoder->crc_received = 0;
}

esp_err_t k230_protocol_encode(uint8_t type, uint16_t sequence,
                                const uint8_t *payload, uint16_t payload_length,
                                uint8_t *out, size_t out_size, size_t *written)
{
    if (out == NULL || written == NULL || (payload_length > 0U && payload == NULL)) {
        return ESP_ERR_INVALID_ARG;
    }

    const size_t frame_size = K230_PROTOCOL_HEADER_SIZE + payload_length + K230_PROTOCOL_TRAILER_SIZE;
    if (out_size < frame_size) {
        return ESP_ERR_INVALID_SIZE;
    }

    out[0] = K230_PROTOCOL_MAGIC_0;
    out[1] = K230_PROTOCOL_MAGIC_1;
    out[2] = K230_PROTOCOL_VERSION;
    out[3] = type;
    out[4] = (uint8_t)(payload_length & 0xFFU);
    out[5] = (uint8_t)(payload_length >> 8);
    out[6] = (uint8_t)(sequence & 0xFFU);
    out[7] = (uint8_t)(sequence >> 8);
    if (payload_length > 0U) {
        memcpy(out + K230_PROTOCOL_HEADER_SIZE, payload, payload_length);
    }

    const uint16_t crc = k230_protocol_crc16(out + 2, 6U + payload_length);
    out[K230_PROTOCOL_HEADER_SIZE + payload_length] = (uint8_t)(crc & 0xFFU);
    out[K230_PROTOCOL_HEADER_SIZE + payload_length + 1U] = (uint8_t)(crc >> 8);
    *written = frame_size;
    return ESP_OK;
}

void k230_protocol_decoder_init(k230_protocol_decoder_t *decoder,
                                uint8_t *payload_storage, size_t payload_capacity)
{
    if (decoder == NULL) {
        return;
    }
    memset(decoder, 0, sizeof(*decoder));
    decoder->payload_storage = payload_storage;
    decoder->payload_capacity = payload_capacity;
    decoder_reset(decoder);
}

esp_err_t k230_protocol_decoder_feed(k230_protocol_decoder_t *decoder,
                                     uint8_t byte, k230_protocol_frame_t *frame)
{
    if (decoder == NULL || frame == NULL || decoder->payload_storage == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    switch (decoder->state) {
    case DECODER_SYNC_MAGIC_0:
        if (byte == K230_PROTOCOL_MAGIC_0) {
            decoder->state = DECODER_SYNC_MAGIC_1;
        }
        return ESP_ERR_NOT_FINISHED;

    case DECODER_SYNC_MAGIC_1:
        if (byte == K230_PROTOCOL_MAGIC_1) {
            decoder->state = DECODER_HEADER;
            decoder->header_length = 0;
        } else {
            decoder->state = byte == K230_PROTOCOL_MAGIC_0 ? DECODER_SYNC_MAGIC_1
                                                             : DECODER_SYNC_MAGIC_0;
        }
        return ESP_ERR_NOT_FINISHED;

    case DECODER_HEADER:
        decoder->header[decoder->header_length++] = byte;
        if (decoder->header_length < sizeof(decoder->header)) {
            return ESP_ERR_NOT_FINISHED;
        }
        if (decoder->header[0] != K230_PROTOCOL_VERSION) {
            decoder_reset(decoder);
            return ESP_ERR_NOT_SUPPORTED;
        }
        decoder->type = decoder->header[1];
        decoder->payload_length = (uint16_t)decoder->header[2] |
                                  ((uint16_t)decoder->header[3] << 8);
        decoder->sequence = (uint16_t)decoder->header[4] |
                            ((uint16_t)decoder->header[5] << 8);
        if ((size_t)decoder->payload_length > decoder->payload_capacity) {
            decoder_reset(decoder);
            return ESP_ERR_INVALID_SIZE;
        }
        decoder->payload_received = 0;
        decoder->crc_received = 0;
        decoder->state = decoder->payload_length == 0U ? DECODER_CRC : DECODER_PAYLOAD;
        return ESP_ERR_NOT_FINISHED;

    case DECODER_PAYLOAD:
        decoder->payload_storage[decoder->payload_received++] = byte;
        if (decoder->payload_received == decoder->payload_length) {
            decoder->state = DECODER_CRC;
        }
        return ESP_ERR_NOT_FINISHED;

    case DECODER_CRC:
        decoder->crc_bytes[decoder->crc_received++] = byte;
        if (decoder->crc_received < sizeof(decoder->crc_bytes)) {
            return ESP_ERR_NOT_FINISHED;
        }

        uint8_t crc_input[6];
        memcpy(crc_input, decoder->header, sizeof(crc_input));
        uint16_t calculated = k230_protocol_crc16(crc_input, sizeof(crc_input));
        if (decoder->payload_length > 0U) {
            /* Compute without allocating a frame-sized temporary buffer. */
            calculated = 0xFFFFU;
            for (size_t i = 0; i < sizeof(crc_input); ++i) {
                calculated ^= (uint16_t)crc_input[i] << 8;
                for (unsigned bit = 0; bit < 8; ++bit) {
                    calculated = (calculated & 0x8000U) != 0U ? (uint16_t)((calculated << 1) ^ 0x1021U)
                                                               : (uint16_t)(calculated << 1);
                }
            }
            for (size_t i = 0; i < decoder->payload_length; ++i) {
                calculated ^= (uint16_t)decoder->payload_storage[i] << 8;
                for (unsigned bit = 0; bit < 8; ++bit) {
                    calculated = (calculated & 0x8000U) != 0U ? (uint16_t)((calculated << 1) ^ 0x1021U)
                                                               : (uint16_t)(calculated << 1);
                }
            }
        }
        const uint16_t received = (uint16_t)decoder->crc_bytes[0] |
                                  ((uint16_t)decoder->crc_bytes[1] << 8);
        if (received != calculated) {
            decoder_reset(decoder);
            return ESP_ERR_INVALID_CRC;
        }

        frame->type = decoder->type;
        frame->sequence = decoder->sequence;
        frame->payload = decoder->payload_storage;
        frame->payload_length = decoder->payload_length;
        decoder_reset(decoder);
        return ESP_OK;

    default:
        decoder_reset(decoder);
        return ESP_ERR_INVALID_STATE;
    }
}
