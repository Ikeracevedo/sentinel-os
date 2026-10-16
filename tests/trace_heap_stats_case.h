#ifndef SENTINEL_TEST_TRACE_HEAP_STATS_CASE_H
#define SENTINEL_TEST_TRACE_HEAP_STATS_CASE_H

#include <sentinel/heap.h>
#include <sentinel/kprintf.h>
#include <sentinel/task.h>
#include <sentinel/trace.h>
#include <stdint.h>

#include "../kernel/core/time.h"

/* Tres muestras permiten comprobar el periodo, no solo la presencia del evento. */
static void trace_heap_stats_case_run(void) {
    trace_event_t event;
    while (trace_pop(&event) == 1) {
    }

    uint32_t first_tick = timer_get_ticks();
    uint32_t last_cycles = 0u;
    uint32_t samples = 0u;
    uint32_t errors = 0u;
    uint32_t free_bytes = 0u;
    uint32_t largest = 0u;
    uint8_t sampler_tid = 0u;

    while ((uint32_t)(timer_get_ticks() - first_tick) < 350u) {
        while (trace_pop(&event) == 1) {
            if (event.type != EV_HEAP_STATS) {
                continue;
            }

            if (event.tid == 0u || event.arg0 > HEAP_SIZE ||
                event.arg1 == 0u || event.arg1 > event.arg0) {
                errors++;
            }
            if (samples != 0u) {
                uint32_t delta = event.cycles - last_cycles;
                /* CPU a 40 MHz: 100 ms son 4 millones de ciclos. */
                if (event.tid != sampler_tid ||
                    delta < 3600000u || delta > 4400000u) {
                    errors++;
                }
            }
            sampler_tid = event.tid;
            last_cycles = event.cycles;
            free_bytes = event.arg0;
            largest = event.arg1;
            samples++;
        }
        task_sleep(10u);
    }

    uint32_t dropped = trace_dropped();
    kprintf("TRACE_HEAP_STATS: muestras=%u libre=%u mayor=%u tid=%u perdidos=%u errores=%u\r\n",
            samples, free_bytes, largest, (uint32_t)sampler_tid,
            dropped, errors);
    kprintf("TRACE_HEAP_STATS_DONE %s\r\n",
            (samples >= 3u && dropped == 0u && errors == 0u)
                ? "PASS" : "FAIL");
}

#endif
