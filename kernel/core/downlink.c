#include <sentinel/downlink.h>
#include <sentinel/frame.h>

void downlink_init(downlink_parser_t *parser) {
    parser->used = 0u;
    parser->expected = 0u;
    parser->crc_errors = 0u;
    parser->invalid_headers = 0u;
}

static void reset_with_suffix(downlink_parser_t *parser, uint8_t last) {
    parser->used = 0u;
    parser->expected = 0u;
    if (last == 0xAAu) {
        parser->bytes[0] = last;
        parser->used = 1u;
    }
}

int downlink_feed(downlink_parser_t *parser, uint8_t byte, downlink_frame_t *out) {
    if (parser->used == 0u) {
        if (byte == 0xAAu) {
            parser->bytes[0] = byte;
            parser->used = 1u;
        }
        return 0;
    }
    if (parser->used == 1u) {
        if (byte == 0x55u) {
            parser->bytes[1] = byte;
            parser->used = 2u;
        } else {
            reset_with_suffix(parser, byte);
        }
        return 0;
    }

    parser->bytes[parser->used++] = byte;
    if (parser->used == 8u) {
        uint8_t version = parser->bytes[2];
        uint8_t kind = parser->bytes[3];
        uint16_t length = (uint16_t)parser->bytes[6] |
                          (uint16_t)((uint16_t)parser->bytes[7] << 8);
        if (version != FRAME_VERSION ||
            (kind != FRAME_CMD && kind != FRAME_TRACE_CFG) ||
            length > DOWNLINK_MAX_PAYLOAD ||
            (kind == FRAME_CMD && length == 0u)) {
            parser->invalid_headers++;
            reset_with_suffix(parser, byte);
            return 0;
        }
        parser->expected = (uint16_t)(length + FRAME_OVERHEAD);
    }
    if (parser->expected == 0u || parser->used < parser->expected) {
        return 0;
    }

    uint16_t length = (uint16_t)(parser->expected - FRAME_OVERHEAD);
    uint16_t got = (uint16_t)parser->bytes[8u + length] |
                   (uint16_t)((uint16_t)parser->bytes[9u + length] << 8);
    uint16_t want = frame_crc16(parser->bytes + 2u, (uint16_t)(length + 6u));
    if (got != want) {
        parser->crc_errors++;
        reset_with_suffix(parser, byte);
        return 0;
    }

    out->kind = parser->bytes[3];
    out->seq = (uint16_t)parser->bytes[4] |
               (uint16_t)((uint16_t)parser->bytes[5] << 8);
    out->length = length;
    for (uint16_t i = 0u; i < length; i++) {
        out->payload[i] = parser->bytes[8u + i];
    }
    parser->used = 0u;
    parser->expected = 0u;
    return 1;
}

int downlink_command_valid(const uint8_t *payload, uint16_t length) {
    if (payload == 0 || length == 0u || length > DOWNLINK_MAX_PAYLOAD) {
        return 0;
    }
    for (uint16_t i = 0u; i < length; i++) {
        if (payload[i] < 0x20u || payload[i] > 0x7Eu) {
            return 0;
        }
    }
    return 1;
}
