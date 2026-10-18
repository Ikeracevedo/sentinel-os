#include <sentinel/downlink.h>
#include <sentinel/frame.h>
#include <assert.h>
#include <stdio.h>

static int feed_all(downlink_parser_t *parser, const uint8_t *wire,
                    uint16_t size, downlink_frame_t *out) {
    int completed = 0;
    for (uint16_t i = 0u; i < size; i++) {
        completed += downlink_feed(parser, wire[i], out);
    }
    return completed;
}

int main(void) {
    downlink_parser_t parser;
    downlink_frame_t out;
    uint8_t wire[FRAME_MAX_BYTES];
    static const uint8_t command[] = {'p', 's'};
    downlink_init(&parser);

    assert(downlink_feed(&parser, 0x42u, &out) == 0);
    assert(downlink_feed(&parser, 0xAAu, &out) == 0);
    uint16_t size = frame_encode(wire, sizeof(wire), FRAME_CMD, 7u,
                                 command, sizeof(command));
    assert(size != 0u);
    assert(feed_all(&parser, wire, size - 1u, &out) == 0);
    assert(downlink_feed(&parser, wire[size - 1u], &out) == 1);
    assert(out.kind == FRAME_CMD && out.seq == 7u);
    assert(out.length == 2u && out.payload[0] == 'p' && out.payload[1] == 's');
    assert(downlink_command_valid(out.payload, out.length));

    wire[8] ^= 1u; /* CRC incorrecto: ningun comando llega al shell. */
    assert(feed_all(&parser, wire, size, &out) == 0);
    assert(parser.crc_errors == 1u);
    wire[8] ^= 1u;
    assert(feed_all(&parser, wire, size, &out) == 1);

    static const uint8_t cfg[] = {FRAME_TRACE_SAMPLE, 8u, 0u};
    size = frame_encode(wire, sizeof(wire), FRAME_TRACE_CFG, 8u,
                        cfg, sizeof(cfg));
    assert(feed_all(&parser, wire, size, &out) == 1);
    assert(out.kind == FRAME_TRACE_CFG && out.length == 3u);
    assert(out.payload[0] == FRAME_TRACE_SAMPLE && out.payload[1] == 8u);

    uint8_t bad[] = {'p', 's', '\r'};
    assert(!downlink_command_valid(bad, sizeof(bad)));
    assert(!downlink_command_valid(command, 0u));

    wire[6] = 0xFFu; /* LEN fuera del maximo se rechaza antes de leer payload. */
    wire[7] = 0xFFu;
    assert(feed_all(&parser, wire, 8u, &out) == 0);
    assert(parser.invalid_headers == 1u);
    size = frame_encode(wire, sizeof(wire), FRAME_CMD, 9u,
                        command, sizeof(command));
    assert(feed_all(&parser, wire, size, &out) == 1);
    assert(out.seq == 9u);

    puts("downlink: CRC, comando, configuracion y resync OK");
    return 0;
}
