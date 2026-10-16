#include <sentinel/kprintf.h>
#include <sentinel/task.h>
#include <sentinel/trace.h>
#include <stdint.h>

#include "../kernel/arch/xtensa/cpu.h"
#include "../kernel/core/sched.h"
#include "../kernel/core/time.h"

#define BENCH_SAMPLES 1000u
#define CONCURRENT_TICKS 1000u
#define TASK_MARKER 0x5E4715A1u
#define ISR_MARKER 0x15A17E57u

static volatile uint32_t g_isr_attempts;

/* Aisla el coste fijo de proteger al productor frente a una ISR. */
__attribute__((noinline)) static void measure_guard_once(void) {
    uint32_t saved_ps = irq_disable();
    __asm__ __volatile__("" ::: "memory");
    irq_restore(saved_ps);
}

static uint32_t measure_guard(void) {
    uint32_t total = 0u;
    uint32_t baseline = 0u;

    for (uint32_t i = 0; i < BENCH_SAMPLES; i++) {
        uint32_t saved_ps = irq_disable();
        uint32_t start = cpu_cycles();
        __asm__ __volatile__("" ::: "memory");
        baseline += cpu_cycles() - start;
        irq_restore(saved_ps);

        saved_ps = irq_disable();
        start = cpu_cycles();
        measure_guard_once();
        total += cpu_cycles() - start;
        irq_restore(saved_ps);
    }

    return (total > baseline)
               ? (total - baseline) / BENCH_SAMPLES
               : 0u;
}

/* Mide solo la decision del filtro, sin escribir ningun evento. */
static uint32_t measure_filter(int enabled) {
    uint32_t total = 0u;
    uint32_t baseline = 0u;
    trace_set_enabled(1);
    (void)trace_set_filter(EV_ALLOC, enabled);

    for (uint32_t i = 0; i < BENCH_SAMPLES; i++) {
        uint32_t saved_ps = irq_disable();
        uint32_t start = cpu_cycles();
        __asm__ __volatile__("" ::: "memory");
        baseline += cpu_cycles() - start;
        irq_restore(saved_ps);

        saved_ps = irq_disable();
        start = cpu_cycles();
        int allowed = trace_filter_allows(EV_ALLOC);
        __asm__ __volatile__("" : : "r"(allowed) : "memory");
        total += cpu_cycles() - start;
        irq_restore(saved_ps);
    }

    return (total > baseline) ? (total - baseline) / BENCH_SAMPLES : 0u;
}

static void trace_tick_probe(void) {
    trace_emit(EV_INJECT, 0u, timer_get_ticks(), ISR_MARKER);
    g_isr_attempts++;
}

static int measure_emit(uint32_t *cycles_out) {
    uint32_t total = 0u;
    uint32_t baseline = 0u;
    trace_event_t event;

    trace_init();

    for (uint32_t i = 0; i < BENCH_SAMPLES; i++) {
        uint32_t saved_ps = irq_disable();
        uint32_t start = cpu_cycles();
        __asm__ __volatile__("" ::: "memory");
        baseline += cpu_cycles() - start;
        irq_restore(saved_ps);

        saved_ps = irq_disable();
        start = cpu_cycles();
        trace_emit(EV_INJECT, 1u, i, ~i);
        total += cpu_cycles() - start;
        irq_restore(saved_ps);

        if (trace_pop(&event) != 1 ||
            event.type != EV_INJECT ||
            event.seq != (uint16_t)i ||
            event.arg0 != i ||
            event.arg1 != ~i) {
            return 0;
        }
    }

    if (trace_dropped() != 0u || trace_emitted() != BENCH_SAMPLES) {
        return 0;
    }

    *cycles_out = (total > baseline)
                    ? (total - baseline) / BENCH_SAMPLES
                    : 0u;
    return 1;
}

static int test_task_and_isr(uint32_t *isr_out, uint32_t *task_out) {
    trace_event_t event;
    uint32_t expected_seq = 0u;
    uint32_t seen_isr = 0u;
    uint32_t seen_task = 0u;
    uint32_t errors = 0u;

    trace_init();
    g_isr_attempts = 0u;

    if (sched_add_tick_hook(trace_tick_probe) != 0) {
        return 0;
    }

    uint32_t start = timer_get_ticks();
    uint32_t last_task_tick = start;

    while ((uint32_t)(timer_get_ticks() - start) < CONCURRENT_TICKS) {
        uint32_t now = timer_get_ticks();

        if (now != last_task_tick) {
            last_task_tick = now;
            trace_emit(EV_INJECT, 1u, now, TASK_MARKER);
        }

        while (trace_pop(&event) == 1) {
            if (event.seq != (uint16_t)expected_seq++) {
                errors++;
            }
            if (event.type == EV_INJECT &&
                event.tid == 0u && event.arg1 == ISR_MARKER) {
                seen_isr++;
            } else if (event.type == EV_INJECT &&
                       event.tid == 1u && event.arg1 == TASK_MARKER) {
                seen_task++;
            }
        }
    }

    sched_remove_tick_hook(trace_tick_probe);

    while (trace_pop(&event) == 1) {
        if (event.seq != (uint16_t)expected_seq++) {
            errors++;
        }
        if (event.type == EV_INJECT &&
            event.tid == 0u && event.arg1 == ISR_MARKER) {
            seen_isr++;
        } else if (event.type == EV_INJECT &&
                   event.tid == 1u && event.arg1 == TASK_MARKER) {
            seen_task++;
        }
    }

    *isr_out = seen_isr;
    *task_out = seen_task;

    return errors == 0u &&
           trace_dropped() == 0u &&
           seen_isr == g_isr_attempts &&
           seen_isr >= CONCURRENT_TICKS &&
           seen_task > 0u;
}

static void trace_board_task(void *arg) {
    (void)arg;

    uint32_t guard_cycles = measure_guard();
    uint32_t filter_on = measure_filter(1);
    uint32_t filter_off = measure_filter(0);
    (void)trace_set_filter(EV_ALLOC, 1);
    uint32_t cycles = 0u;
    uint32_t isr_events = 0u;
    uint32_t task_events = 0u;

    int bench_ok = measure_emit(&cycles);
    int concurrent_ok = test_task_and_isr(&isr_events, &task_events);

    kprintf("TRACE_FILTER: on=%u ciclos, off=%u ciclos\r\n",
            filter_on, filter_off);
    kprintf("TRACE_FILTER_DONE %s\r\n",
            (filter_on < 5u && filter_off < 5u) ? "PASS" : "FAIL");
    kprintf("TRACE_BOARD: guard=%u ciclos, emit=%u ciclos, ISR=%u, tarea=%u, perdidos=%u\r\n",
            guard_cycles, cycles, isr_events, task_events, trace_dropped());
    kprintf("TRACE_BOARD_DONE %s\r\n",
            (bench_ok && concurrent_ok && cycles < 50u) ? "PASS" : "FAIL");
}

/* El enlazador invoca esta funcion solo en la imagen de prueba. */
int32_t __real_shell_start(void);

int32_t __wrap_shell_start(void) {
    if (task_create("trace-board", trace_board_task, 0, 0u, 0u) < 0) {
        kprintf("TRACE_BOARD_DONE FAIL: no se pudo crear la tarea\r\n");
    }
    return __real_shell_start();
}
