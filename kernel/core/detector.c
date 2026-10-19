/* E10: el analisis corre en el ISR; eventos y UART se difieren a una tarea.
 * No usa mutexes ni heap. Una misma lista de tareas se alerta una sola vez
 * mientras el ciclo siga presente. */
#include <sentinel/detector.h>
#include <sentinel/graph.h>
#include <sentinel/task.h>
#include <sentinel/sync.h>
#include <sentinel/kprintf.h>
#include <sentinel/probe.h>
#include "sched.h"
#include "time.h"
#include "../arch/xtensa/cpu.h"

#define DETECTOR_PERIOD_MS 500u
#define DETECTOR_PRINT_POLL_MS 20u

typedef struct {
    graph_cycle_t cycle;
    uint32_t mutex_id[MAX_TASKS];
} pending_alert_t;

static graph_t g_snapshot;
static graph_cycle_t g_latched;
static pending_alert_t g_pending_alert;
static volatile uint8_t g_pending;
static volatile detector_stats_t g_stats;
static uint32_t g_next_due;

static int same_cycle(const graph_cycle_t *a, const graph_cycle_t *b) {
    if (a->count != b->count) {
        return 0;
    }
    for (uint32_t i = 0; i < a->count; i++) {
        if (a->tid[i] != b->tid[i]) {
            return 0;
        }
    }
    return 1;
}

static void detector_tick(void) {
    uint32_t now = uptime_ms();
    if ((int32_t)(now - g_next_due) < 0) {
        return;
    }
    g_next_due = now + DETECTOR_PERIOD_MS;
    uint32_t started = cpu_cycles();
    graph_cycle_t found;
    uint32_t graph_cycles = graph_build(&g_snapshot);
    uint32_t search_started = cpu_cycles();
    int has_cycle = graph_find_cycle(&g_snapshot, &found);
    uint32_t dfs_cycles = cpu_cycles() - search_started;
    if (graph_cycles > g_stats.max_graph_cycles) {
        g_stats.max_graph_cycles = graph_cycles;
    }
    if (dfs_cycles > g_stats.max_dfs_cycles) {
        g_stats.max_dfs_cycles = dfs_cycles;
    }
    if (!has_cycle) {
        g_latched.count = 0u;
    } else if (!same_cycle(&found, &g_latched)) {
        g_latched = found;
        g_pending_alert.cycle = found;
        for (uint32_t i = 0; i < found.count; i++) {
            tcb_t *t = task_get(found.tid[i]);
            mutex_t *m = (t != NULL && t->waiting_kind == WAIT_MUTEX)
                         ? (mutex_t *)t->waiting_on : NULL;
            g_pending_alert.mutex_id[i] = (m != NULL) ? m->id : 0u;
        }
        g_pending = 1u;
        g_stats.alerts++;
    }
    uint32_t cost = cpu_cycles() - started;
    g_stats.runs++;
    g_stats.last_cycles = cost;
    if (cost > g_stats.max_cycles) {
        g_stats.max_cycles = cost;
    }
}

static void detector_print_task(void *arg) {
    (void)arg;
    for (;;) {
        pending_alert_t alert;
        uint32_t ps = irq_save();
        uint8_t ready = g_pending;
        if (ready) {
            alert = g_pending_alert;
            g_pending = 0u;
        }
        irq_restore(ps);
        if (ready) {
#if SENTINEL_TRACE
            for (uint32_t i = 0; i < alert.cycle.count; i++) {
                trace_probe_emit(EV_ALERT, alert.cycle.tid[i], alert.mutex_id[i],
                                 alert.cycle.tid[(i + 1u) % alert.cycle.count]);
            }
#endif
            kprintf("[ALERTA] DEADLOCK: ");
            for (uint32_t i = 0; i < alert.cycle.count; i++) {
                uint8_t tid = alert.cycle.tid[i];
                tcb_t *t = task_get(tid);
                kprintf("%s(tid %u) --mutex #%u--> ",
                        (t != NULL) ? t->name : "?", (uint32_t)tid,
                        alert.mutex_id[i]);
            }
            uint8_t first = alert.cycle.tid[0];
            tcb_t *t = task_get(first);
            kprintf("%s(tid %u)\r\n", (t != NULL) ? t->name : "?",
                    (uint32_t)first);
        }
        task_sleep(DETECTOR_PRINT_POLL_MS);
    }
}

int detector_start(void) {
    int32_t tid = task_create("detector", detector_print_task, NULL, 2u, 0u);
    if (tid < 0) {
        return -1;
    }
    g_next_due = uptime_ms() + DETECTOR_PERIOD_MS;
    if (sched_add_tick_hook(detector_tick) != 0) {
        (void)task_kill((uint32_t)tid);
        return -1;
    }
    return 0;
}

void detector_stats(detector_stats_t *out) {
    if (out == NULL) {
        return;
    }
    uint32_t ps = irq_save();
    out->runs = g_stats.runs;
    out->alerts = g_stats.alerts;
    out->last_cycles = g_stats.last_cycles;
    out->max_cycles = g_stats.max_cycles;
    out->max_graph_cycles = g_stats.max_graph_cycles;
    out->max_dfs_cycles = g_stats.max_dfs_cycles;
    irq_restore(ps);
}
