#include <sentinel/trace.h>
#include <assert.h>
#include <stdint.h>
#include <stdio.h>

int main(void) {
    trace_event_t event;
    uint32_t evaluated = 0u;

    trace_init();
    assert(trace_enabled == 1u);
    assert(trace_get_filter(EV_ALLOC) == 1);

    trace_set_enabled(0);
    trace_probe_emit(EV_ALLOC, 2u, ++evaluated, 0u);
    assert(evaluated == 0u);
    assert(trace_count() == 0u);

    /* El latido usa la ruta directa y cruza el apagado global. */
    trace_emit(EV_HEARTBEAT, 0u, 100u, 0u);
    assert(trace_pop(&event) == 1 && event.type == EV_HEARTBEAT);

    trace_set_enabled(1);
    assert(trace_set_filter(EV_ALLOC, 0) == 0);
    trace_probe_emit(EV_ALLOC, 2u, ++evaluated, 0u);
    assert(evaluated == 0u && trace_count() == 0u);
    assert(trace_set_filter(EV_HEARTBEAT, 0) == -1);
    assert(trace_get_filter(EV_HEARTBEAT) == 1);

    assert(trace_set_filter(EV_ALLOC, 1) == 0);
    trace_probe_emit(EV_ALLOC, 2u, ++evaluated, 0u);
    assert(evaluated == 1u);
    assert(trace_pop(&event) == 1 && event.type == EV_ALLOC && event.arg0 == 1u);

    assert(trace_set_sample_rate(0u) == -1);
    assert(trace_set_sample_rate(3u) == 0);
    for (uint32_t i = 0; i < 9u; i++) {
        trace_probe_emit(EV_SWITCH_IN, 2u, i, 0u);
    }
    assert(trace_count() == 3u);
    for (uint32_t i = 0; i < 3u; i++) {
        assert(trace_pop(&event) == 1);
        assert(event.type == EV_SWITCH_IN && event.arg0 == i * 3u + 2u);
    }

    trace_probe_emit(EV_FREE, 2u, 7u, 0u);
    assert(trace_pop(&event) == 1 && event.type == EV_FREE);
    assert(trace_count() == 0u);
    assert(trace_dropped() == 0u);

    puts("PASS: filtros, muestreo y heartbeat incondicional");
    return 0;
}
