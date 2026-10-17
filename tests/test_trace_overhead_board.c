#include <sentinel/heap.h>
#include <sentinel/kprintf.h>
#include <sentinel/sync.h>
#include <sentinel/task.h>
#include <stdint.h>

#include "../kernel/arch/xtensa/cpu.h"
#include "../kernel/core/sched.h"
#include "../kernel/core/time.h"

#if SENTINEL_TRACE
#include <sentinel/trace.h>
#endif

#define OVERHEAD_WORKERS 4u
#define OVERHEAD_RUNS 5u
#define OVERHEAD_RUN_MS 60000u

static mutex_t g_bench_mutex;
static volatile uint32_t g_stop;
static volatile uint32_t g_operations[OVERHEAD_WORKERS];
static volatile uint32_t g_errors;

/* Cuatro trabajadores mas el coordinador son las cinco tareas de la carga.
 * Cada iteracion combina heap, mutex y un cambio de tarea voluntario. */
static void overhead_worker(void *arg) {
    uint32_t id = (uint32_t)(uintptr_t)arg;
    while (g_stop == 0u) {
        void *block = kmalloc(32u + id * 4u);
        if (block == NULL) {
            g_errors++;
        } else {
            kfree(block);
        }

        if (mutex_lock(&g_bench_mutex) != 0) {
            g_errors++;
        } else {
            for (volatile uint32_t spin = 0; spin < 20u + id * 5u; spin++) {
            }
            if (mutex_unlock(&g_bench_mutex) != 0) {
                g_errors++;
            }
        }

        g_operations[id]++;
        if (id == 0u) {
            sched_yield();
        }
        task_sleep(8u + id);
    }
}

static uint32_t total_operations(void) {
    uint32_t total = 0u;
    for (uint32_t i = 0; i < OVERHEAD_WORKERS; i++) {
        total += g_operations[i];
    }
    return total;
}

#if SENTINEL_TRACE
static void drain_trace(void) {
    trace_event_t event;
    while (trace_pop(&event) == 1) {
    }
}
#else
static void drain_trace(void) {
}
#endif

static void overhead_coordinator(void *arg) {
    (void)arg;
    mutex_init(&g_bench_mutex, "trace-bench");
    uint32_t created = 0u;
    for (uint32_t i = 0; i < OVERHEAD_WORKERS; i++) {
        if (task_create("bench-worker", overhead_worker,
                        (void *)(uintptr_t)i, 1u, 0u) >= 0) {
            created++;
        }
    }
    if (created != OVERHEAD_WORKERS) {
        kprintf("TRACE_OVERHEAD_DONE FAIL: solo %u trabajadores\r\n", created);
        return;
    }

    task_sleep(20u);
    for (uint32_t run = 1u; run <= OVERHEAD_RUNS; run++) {
        drain_trace();
        sched_bench_reset();
        uint32_t before_ops = total_operations();
        uint32_t before_errors = g_errors;
        uint64_t before_idle = task_get(0u)->cpu_cycles;
#if SENTINEL_TRACE
        uint32_t before_emitted = trace_emitted();
        uint32_t before_dropped = trace_dropped();
#endif
        uint32_t start_cycles = cpu_cycles();
        uint32_t start_ms = timer_get_ticks();

        while ((uint32_t)(timer_get_ticks() - start_ms) < OVERHEAD_RUN_MS) {
            drain_trace();
            task_sleep(1u);
        }

        uint32_t elapsed_cycles = cpu_cycles() - start_cycles;
        uint32_t idle_cycles =
            (uint32_t)(task_get(0u)->cpu_cycles - before_idle);
        sched_bench_stats_t stats;
        sched_bench_snapshot(&stats);
        uint32_t ops = total_operations() - before_ops;
        uint32_t errors = g_errors - before_errors;
#if SENTINEL_TRACE
        uint32_t emitted = trace_emitted() - before_emitted;
        uint32_t dropped = trace_dropped() - before_dropped;
#else
        uint32_t emitted = 0u;
        uint32_t dropped = 0u;
#endif
        /* La UART del bare metal puede perder bytes al imprimir dos lineas
         * seguidas. Repetirlas con pausa ocurre fuera de la ventana medida. */
        for (uint32_t copy = 0u; copy < 12u; copy++) {
            kprintf("TRACE_OVERHEAD: trace=%u run=%u ms=%u elapsed=%u idle=%u switches=%u sum=%u max=%u sq_hi=%u sq_lo=%u ops=%u emitted=%u dropped=%u errors=%u\r\n",
                    (uint32_t)SENTINEL_TRACE, run, OVERHEAD_RUN_MS,
                    elapsed_cycles, idle_cycles, stats.count,
                    stats.sum_cycles, stats.max_cycles,
                    (uint32_t)(stats.sum_squares >> 32),
                    (uint32_t)stats.sum_squares,
                    ops, emitted, dropped, errors);
            kprintf("TRACE_OVERHEAD_FULL: trace=%u run=%u switches=%u sum=%u max=%u sq_hi=%u sq_lo=%u\r\n",
                    (uint32_t)SENTINEL_TRACE, run, stats.full_count,
                    stats.full_sum_cycles, stats.full_max_cycles,
                    (uint32_t)(stats.full_sum_squares >> 32),
                    (uint32_t)stats.full_sum_squares);
            task_sleep(60u);
        }
        if (errors != 0u || dropped != 0u || stats.count == 0u ||
            stats.full_count == 0u ||
            (stats.count > stats.full_count + 2u) ||
            (stats.full_count > stats.count + 2u) ||
            ops < 1000u || elapsed_cycles == 0u ||
            idle_cycles > elapsed_cycles) {
            kprintf("TRACE_OVERHEAD_DONE FAIL\r\n");
            g_stop = 1u;
            return;
        }
    }
    g_stop = 1u;
    kprintf("TRACE_OVERHEAD_DONE PASS\r\n");
}

int32_t __real_shell_start(void);

int32_t __wrap_shell_start(void) {
    if (task_create("trace-bench", overhead_coordinator, 0, 0u, 0u) < 0) {
        kprintf("TRACE_OVERHEAD_DONE FAIL: coordinador\r\n");
    }
    return __real_shell_start();
}
