#include <sentinel/kprintf.h>
#include <sentinel/probe.h>
#include <sentinel/task.h>
#include <sentinel/trace.h>
#include <stdint.h>

#include "../kernel/core/sched.h"
#include "../kernel/core/time.h"
#include "trace_heap_lock_case.h"
#include "trace_heap_stats_case.h"

#define WORKERS 4u
#define ACTIVE_MS 100u
#define QUANTUM_MS 30u

static volatile uint32_t g_stop_workers;
static volatile uint32_t g_worker_loops[WORKERS];

typedef struct {
    uint32_t pairs;
    uint32_t reasons[TRACE_SWITCH_EXIT + 1u];
    uint32_t tids_seen;
    uint32_t errors;
    uint32_t cycles_min;
    uint32_t cycles_max;
    uint32_t cycles_sum;
    uint16_t last_seq;
    uint8_t next_tid;
    uint8_t have_seq;
    uint8_t pending_out;
    uint8_t saw_out;
    uint8_t orphan_start;
} sched_check_t;

static void worker_task(void *arg) {
    uint32_t index = (uint32_t)(uintptr_t)arg;

    while (g_stop_workers == 0u) {
        g_worker_loops[index]++;

        if (index == 0u) {
            task_sleep(2u);
        } else if (index == 1u) {
            for (volatile uint32_t i = 0; i < 4000u; i++) {
            }
            sched_yield();
        } else if (index == 2u) {
            for (volatile uint32_t i = 0; i < 20000u; i++) {
            }
        } else {
            task_sleep(3u);
        }
    }
}

static void check_event(sched_check_t *check, const trace_event_t *event) {
    if (check->have_seq != 0u &&
        event->seq != (uint16_t)(check->last_seq + 1u)) {
        check->errors++;
    }
    check->last_seq = event->seq;
    check->have_seq = 1u;

    if (event->type == EV_SWITCH_OUT) {
        if (check->pending_out != 0u ||
            event->tid >= MAX_TASKS || event->arg0 >= MAX_TASKS ||
            event->arg1 < TRACE_SWITCH_QUANTUM ||
            event->arg1 > TRACE_SWITCH_EXIT) {
            check->errors++;
            return;
        }
        check->pending_out = 1u;
        check->saw_out = 1u;
        check->next_tid = (uint8_t)event->arg0;
        check->tids_seen |= 1u << event->tid;
        check->tids_seen |= 1u << event->arg0;
        check->reasons[event->arg1]++;
    } else if (event->type == EV_SWITCH_IN) {
        /* El vaciado inicial puede cortar una pareja: OUT ya se consumio
         * antes de empezar la prueba e IN llega en la excepcion siguiente. */
        if (check->saw_out == 0u && check->pending_out == 0u &&
            check->orphan_start == 0u) {
            check->orphan_start = 1u;
            return;
        }
        if (check->pending_out == 0u || event->tid != check->next_tid ||
            event->arg0 == 0u) {
            check->errors++;
            return;
        }
        check->pending_out = 0u;
        check->pairs++;
        if (check->pairs == 1u || event->arg0 < check->cycles_min) {
            check->cycles_min = event->arg0;
        }
        if (event->arg0 > check->cycles_max) {
            check->cycles_max = event->arg0;
        }
        check->cycles_sum += event->arg0;
    }
}

static void drain_events(sched_check_t *check) {
    trace_event_t event;
    while (trace_pop(&event) == 1) {
        check_event(check, &event);
    }
}

static uint32_t count_tids(uint32_t mask) {
    uint32_t count = 0u;
    while (mask != 0u) {
        count += mask & 1u;
        mask >>= 1;
    }
    return count;
}

static void trace_sched_board_task(void *arg) {
    (void)arg;
    sched_check_t check = {0};
    trace_event_t ignored;
    int created = 1;

    /* El arranque ya emitio eventos; empezamos con una pareja completa nueva. */
    while (trace_pop(&ignored) == 1) {
    }

    for (uint32_t i = 0; i < WORKERS; i++) {
        if (task_create("trace-worker", worker_task,
                        (void *)(uintptr_t)i, 1u, 0u) < 0) {
            created = 0;
        }
    }

    uint32_t start = timer_get_ticks();
    while ((uint32_t)(timer_get_ticks() - start) < ACTIVE_MS) {
        drain_events(&check);
        task_sleep(1u);
    }

    /* Dejar correr a los trabajadores permite observar un quantum agotado. */
    task_sleep(QUANTUM_MS);
    drain_events(&check);

    g_stop_workers = 1u;
    task_sleep(20u);
    drain_events(&check);

    uint32_t workers_ran = 0u;
    for (uint32_t i = 0; i < WORKERS; i++) {
        workers_ran += (g_worker_loops[i] != 0u);
    }

    uint32_t dropped = trace_dropped();
    kprintf("TRACE_SCHED_BOARD: pares=%u tareas=%u workers=%u quantum=%u block=%u yield=%u preempt=%u exit=%u perdidos=%u errores=%u inicio_parcial=%u fin_parcial=%u\r\n",
            check.pairs, count_tids(check.tids_seen), workers_ran,
            check.reasons[TRACE_SWITCH_QUANTUM],
            check.reasons[TRACE_SWITCH_BLOCK],
            check.reasons[TRACE_SWITCH_YIELD],
            check.reasons[TRACE_SWITCH_PREEMPT],
            check.reasons[TRACE_SWITCH_EXIT], dropped, check.errors,
            check.orphan_start, check.pending_out);
    kprintf("TRACE_SCHED_CYCLES: minimo=%u media=%u maximo=%u\r\n",
            check.cycles_min,
            check.pairs != 0u ? check.cycles_sum / check.pairs : 0u,
            check.cycles_max);

    int passed = created && workers_ran == WORKERS &&
                 check.pairs >= 20u && count_tids(check.tids_seen) >= 5u &&
                 check.reasons[TRACE_SWITCH_QUANTUM] > 0u &&
                 check.reasons[TRACE_SWITCH_BLOCK] > 0u &&
                 check.reasons[TRACE_SWITCH_YIELD] > 0u &&
                 check.reasons[TRACE_SWITCH_PREEMPT] > 0u &&
                 dropped == 0u && check.errors == 0u;
    kprintf("TRACE_SCHED_DONE %s\r\n", passed ? "PASS" : "FAIL");
    trace_heap_lock_case_run();
    trace_heap_stats_case_run();
}

int32_t __real_shell_start(void);

int32_t __wrap_shell_start(void) {
    if (task_create("trace-sched", trace_sched_board_task, 0, 0u, 0u) < 0) {
        kprintf("TRACE_SCHED_DONE FAIL: no se pudo crear la tarea\r\n");
    }
    return __real_shell_start();
}
