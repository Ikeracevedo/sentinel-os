#ifndef SENTINEL_FRAME_H
#define SENTINEL_FRAME_H

#include <stdint.h>

#define FRAME_VERSION       1u
#define FRAME_MAX_PAYLOAD 512u
#define FRAME_OVERHEAD     10u
#define FRAME_MAX_BYTES   (FRAME_MAX_PAYLOAD + FRAME_OVERHEAD)

enum {
    FRAME_EVENTS    = 0x01,
    FRAME_STATS     = 0x02,
    FRAME_LOG       = 0x03,
    FRAME_ALERT     = 0x04,
    FRAME_HELLO     = 0x05,
    FRAME_HEARTBEAT = 0x06,
    FRAME_CMD       = 0x81,
    FRAME_TRACE_CFG = 0x82,
    FRAME_ACK       = 0x83
};

enum {
    FRAME_TRACE_ENABLE = 0x01,
    FRAME_TRACE_FILTER = 0x02,
    FRAME_TRACE_SAMPLE = 0x03
};

/* CRC sobre VER..PAYLOAD, excluyendo los dos bytes de sync. */
uint16_t frame_crc16(const uint8_t *bytes, uint16_t len);

/* Escribe sync, cabecera, payload y CRC; devuelve bytes escritos o 0 si no cabe. */
uint16_t frame_encode(uint8_t *out, uint16_t capacity, uint8_t type,
                      uint16_t seq, const uint8_t *payload, uint16_t length);

#endif
