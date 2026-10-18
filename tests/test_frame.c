#include <sentinel/frame.h>
#include <assert.h>
#include <stdint.h>
#include <stdio.h>

int main(void) {
    static const uint8_t vector[] = "123456789";
    assert(frame_crc16(vector, 9u) == 0x29B1u);

    uint8_t payload[256];
    for (uint32_t i = 0; i < sizeof(payload); i++) {
        payload[i] = (uint8_t)i;
    }
    uint8_t encoded[FRAME_MAX_BYTES];
    uint16_t n = frame_encode(encoded, sizeof(encoded), FRAME_EVENTS,
                              0xFFFFu, payload, sizeof(payload));
    assert(n == sizeof(payload) + FRAME_OVERHEAD);
    assert(encoded[0] == 0xAAu && encoded[1] == 0x55u);
    assert(encoded[2] == FRAME_VERSION && encoded[3] == FRAME_EVENTS);
    assert(encoded[4] == 0xFFu && encoded[5] == 0xFFu);
    assert(encoded[6] == 0x00u && encoded[7] == 0x01u);
    for (uint32_t i = 0; i < sizeof(payload); i++) {
        assert(encoded[8u + i] == payload[i]);
    }
    uint16_t crc = frame_crc16(encoded + 2u, (uint16_t)(n - 4u));
    assert(encoded[n - 2u] == (uint8_t)crc);
    assert(encoded[n - 1u] == (uint8_t)(crc >> 8));
    assert(frame_encode(encoded, 20u, FRAME_EVENTS, 0u,
                        payload, sizeof(payload)) == 0u);
    assert(frame_encode(encoded, sizeof(encoded), FRAME_EVENTS, 0u,
                        payload, 513u) == 0u);
    puts("frame: CRC y formato OK");
    return 0;
}
