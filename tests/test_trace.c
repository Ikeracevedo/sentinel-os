#include <sentinel/trace.h>
#include <assert.h>
#include <stdint.h>
#include <stdio.h>

#define STRESS_EVENTS 1000000u

static void test_full_buffer(void) {
    trace_event_t event;

    trace_init();

    for (uint32_t i = 0; i < TRACE_BUFFER_CAPACITY; i++) {
        trace_emit(EV_ALLOC, (uint8_t)i, i, i ^ 0xA5A5A5A5u);
    }

    assert(trace_count() == TRACE_BUFFER_CAPACITY);
    assert(trace_emitted() == TRACE_BUFFER_CAPACITY);
    assert(trace_dropped() == 0u);

    trace_emit(EV_ALLOC, 0u, 999u, 999u);

    assert(trace_count() == TRACE_BUFFER_CAPACITY);
    assert(trace_emitted() == TRACE_BUFFER_CAPACITY);
    assert(trace_dropped() == 1u);

    for (uint32_t i = 0; i < TRACE_BUFFER_CAPACITY; i++) {
        assert(trace_pop(&event) == 1);
        assert(event.type == EV_ALLOC);
        assert(event.tid == (uint8_t)i);
        assert(event.seq == (uint16_t)i);
        assert(event.arg0 == i);
        assert(event.arg1 == (i ^ 0xA5A5A5A5u));
    }

    assert(trace_count() == 0u);
    assert(trace_pop(&event) == 0);

    /* Tras vaciarlo, el productor debe volver a usar los espacios liberados. */
    trace_emit(EV_FREE, 7u, 123u, 456u);
    assert(trace_count() == 1u);
    assert(trace_emitted() == TRACE_BUFFER_CAPACITY + 1u);
    assert(trace_dropped() == 1u);
    assert(trace_pop(&event) == 1);
    assert(event.type == EV_FREE);
    assert(event.seq == (uint16_t)TRACE_BUFFER_CAPACITY);
    assert(event.arg0 == 123u);
    assert(event.arg1 == 456u);
    assert(trace_count() == 0u);

    assert(trace_pop(NULL) == 0);
}

static void test_million_events(void) {
    trace_event_t event;
    uint32_t produced = 0;
    uint32_t consumed = 0;

    trace_init();

    while (produced < STRESS_EVENTS) {
        uint32_t chunk =
            1u + ((produced * 37u + 503u) % TRACE_BUFFER_CAPACITY);

        if (chunk > STRESS_EVENTS - produced) {
            chunk = STRESS_EVENTS - produced;
        }

        for (uint32_t i = 0; i < chunk; i++) {
            uint32_t n = produced + i;
            trace_emit(EV_LOCK_ACQ, (uint8_t)n, n, ~n);
        }

        assert(trace_count() == chunk);

        for (uint32_t i = 0; i < chunk; i++) {
            uint32_t n = consumed + i;

            assert(trace_pop(&event) == 1);
            assert(event.cycles == n + 1u);
            assert(event.type == EV_LOCK_ACQ);
            assert(event.tid == (uint8_t)n);
            assert(event.seq == (uint16_t)n);
            assert(event.arg0 == n);
            assert(event.arg1 == ~n);
        }

        produced += chunk;
        consumed += chunk;
        assert(trace_count() == 0u);
    }

    assert(produced == STRESS_EVENTS);
    assert(consumed == STRESS_EVENTS);
    assert(trace_emitted() == STRESS_EVENTS);
    assert(trace_dropped() == 0u);
    assert(trace_pop(&event) == 0);
}

int main(void) {
    test_full_buffer();
    test_million_events();

    printf("PASS: buffer lleno y %u eventos verificados\n",
           STRESS_EVENTS);
    return 0;
}
