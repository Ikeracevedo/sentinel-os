#ifndef SENTINEL_DOWNLINK_H
#define SENTINEL_DOWNLINK_H

#include <stdint.h>

#define DOWNLINK_MAX_PAYLOAD 127u
#define DOWNLINK_MAX_BYTES (DOWNLINK_MAX_PAYLOAD + 10u)

typedef struct {
    uint8_t bytes[DOWNLINK_MAX_BYTES];
    uint16_t used;
    uint16_t expected;
    uint32_t crc_errors;
    uint32_t invalid_headers;
} downlink_parser_t;

typedef struct {
    uint8_t kind;
    uint16_t seq;
    uint16_t length;
    uint8_t payload[DOWNLINK_MAX_PAYLOAD];
} downlink_frame_t;

void downlink_init(downlink_parser_t *parser);
/* Devuelve 1 solo al recibir una trama completa con CRC valido. */
int downlink_feed(downlink_parser_t *parser, uint8_t byte, downlink_frame_t *out);
int downlink_command_valid(const uint8_t *payload, uint16_t length);

#endif
