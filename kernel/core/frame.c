#include <sentinel/frame.h>

uint16_t frame_crc16(const uint8_t *bytes, uint16_t len) {
    uint16_t crc = 0xFFFFu;
    for (uint16_t i = 0; i < len; i++) {
        crc ^= (uint16_t)bytes[i] << 8;
        for (uint32_t bit = 0; bit < 8u; bit++) {
            crc = (uint16_t)((crc << 1) ^ ((crc & 0x8000u) ? 0x1021u : 0u));
        }
    }
    return crc;
}

uint16_t frame_encode(uint8_t *out, uint16_t capacity, uint8_t type,
                      uint16_t seq, const uint8_t *payload, uint16_t length) {
    if (out == 0 || length > FRAME_MAX_PAYLOAD ||
        (length != 0u && payload == 0) ||
        (uint32_t)capacity < (uint32_t)length + FRAME_OVERHEAD) {
        return 0;
    }
    out[0] = 0xAAu;
    out[1] = 0x55u;
    out[2] = FRAME_VERSION;
    out[3] = type;
    out[4] = (uint8_t)seq;
    out[5] = (uint8_t)(seq >> 8);
    out[6] = (uint8_t)length;
    out[7] = (uint8_t)(length >> 8);
    for (uint16_t i = 0; i < length; i++) {
        out[8u + i] = payload[i];
    }
    uint16_t crc = frame_crc16(out + 2u, (uint16_t)(length + 6u));
    out[8u + length] = (uint8_t)crc;
    out[9u + length] = (uint8_t)(crc >> 8);
    return (uint16_t)(length + FRAME_OVERHEAD);
}
