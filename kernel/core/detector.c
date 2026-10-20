/* E10: el grafo se analiza en el ISR sin mutex ni heap. Las metricas del heap,
 * eventos y UART se procesan en una tarea. Un ciclo persistente se alerta una
 * sola vez mientras permanezca presente. */
#include <sentinel/detector.h>
#include <sentinel/graph.h>
#include <sentinel/task.h>
#include <sentinel/sync.h>
#include <sentinel/heap.h>
#include <sentinel/kprintf.h>
#include <sentinel/probe.h>
#include "sched.h"
#include "time.h"
#include "../arch/xtensa/cpu.h"

#define DETECTOR_PERIOD_MS 500u
#define DETECTOR_PRINT_POLL_MS 20u
#define FRAG_PERIOD_MS 1000u
#define STARVE_DEFAULT_MS 500u

typedef struct {
    graph_cycle_t cycle;
    uint32_t mutex_id[MAX_TASKS];
} pending_alert_t;

typedef struct {
    uint8_t tid;
    uint8_t priority;
    uint32_t instance_id;
    uint32_t wait_ms;
} pending_starve_t;

static graph_t g_snapshot;
static graph_cycle_t g_latched;
static pending_alert_t g_pending_alert;
static volatile uint8_t g_pending;
static pending_starve_t g_pending_starve;
static volatile uint8_t g_starve_pending;
static uint32_t g_starve_latched_instance[MAX_TASKS];
static uint32_t g_starve_last_alert_ms[MAX_TASKS];
static volatile uint32_t g_starve_threshold_ms = STARVE_DEFAULT_MS;
static volatile detector_stats_t g_stats;
static uint32_t g_next_due;
static uint32_t g_next_frag_due;
static uint8_t g_frag_level;

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

static void detector_check_starvation(uint32_t now) {
    g_stats.starve_checks++;
    for (uint32_t tid = 1u; tid < MAX_TASKS; tid++) {
        tcb_t *t = task_get(tid);
        if (t == NULL) {
            g_starve_latched_instance[tid] = 0u;
            g_starve_last_alert_ms[tid] = 0u;
            continue;
        }
        if (t->state != TASK_READY) {
            continue;
        }
        uint32_t wait_ms = now - t->ready_since;
        if (wait_ms > t->max_ready_wait) {
            t->max_ready_wait = wait_ms;
        }
        if (wait_ms > g_stats.starve_max_wait_ms) {
            g_stats.starve_max_wait_ms = wait_ms;
        }
        if (wait_ms <= g_starve_threshold_ms) {
            continue;
        }
        if ((g_starve_latched_instance[tid] == t->instance_id &&
             now - g_starve_last_alert_ms[tid] < 10000u) || g_starve_pending) {
            continue;
        }
        g_starve_latched_instance[tid] = t->instance_id;
        g_starve_last_alert_ms[tid] = now;
        g_pending_starve.tid = (uint8_t)tid;
        g_pending_starve.priority = t->priority;
        g_pending_starve.instance_id = t->instance_id;
        g_pending_starve.wait_ms = wait_ms;
        g_starve_pending = 1u;
        g_stats.starve_alerts++;
    }
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
    detector_check_starvation(now);
    uint32_t cost = cpu_cycles() - started;
    g_stats.runs++;
    g_stats.last_cycles = cost;
    if (cost > g_stats.max_cycles) {
        g_stats.max_cycles = cost;
    }
}

static void detector_check_fragmentation(void) {
    uint32_t now = uptime_ms();
    if ((int32_t)(now - g_next_frag_due) < 0) {
        return;
    }
    g_next_frag_due = now + FRAG_PERIOD_MS;

    heap_stats_t heap;
    heap_stats(&heap);
    uint32_t percent = heap_frag_percent(&heap);
    uint8_t level = percent > 60u ? 2u : percent > 30u ? 1u : 0u;
    g_stats.frag_checks++;
    g_stats.frag_percent = percent;
    g_stats.frag_free = heap.free;
    g_stats.frag_largest = heap.largest_free_block;

    if (level > g_frag_level) {
        g_stats.frag_alerts++;
#if SENTINEL_TRACE
        trace_probe_emit(EV_ALERT, 0u, percent, heap.largest_free_block);
#endif
        kprintf("[ALERTA] FRAGMENTACION %s: frag=%u%% libre=%u B mayor=%u B\r\n",
                level == 2u ? "CRITICA" : "ADVERTENCIA", percent,
                heap.free, heap.largest_free_block);
    }
    g_frag_level = level;
}

static void detector_print_task(void *arg) {
    (void)arg;
    for (;;) {
        pending_alert_t alert;
        pending_starve_t starvation;
        uint32_t ps = irq_save();
        uint8_t ready = g_pending;
        if (ready) {
            alert = g_pending_alert;
            g_pending = 0u;
        }
        uint8_t starve_ready = g_starve_pending;
        if (starve_ready) {
            starvation = g_pending_starve;
            g_starve_pending = 0u;
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
        if (starve_ready) {
            tcb_t *t = task_get(starvation.tid);
            const char *name = (t != NULL &&
                                t->instance_id == starvation.instance_id)
                               ? t->name : "?";
#if SENTINEL_TRACE
            trace_probe_emit(EV_ALERT, starvation.tid, starvation.priority,
                             starvation.wait_ms);
#endif
            kprintf("[ALERTA] INANICION: %s(tid %u) prioridad=%u "
                    "espera=%u ms umbral=%u ms\r\n", name,
                    (uint32_t)starvation.tid, (uint32_t)starvation.priority,
                    starvation.wait_ms, g_starve_threshold_ms);
        }
        detector_check_fragmentation();
        task_sleep(DETECTOR_PRINT_POLL_MS);
    }
}

int detector_start(void) {
    g_next_frag_due = uptime_ms() + FRAG_PERIOD_MS;
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
    out->frag_checks = g_stats.frag_checks;
    out->frag_alerts = g_stats.frag_alerts;
    out->frag_percent = g_stats.frag_percent;
    out->frag_free = g_stats.frag_free;
    out->frag_largest = g_stats.frag_largest;
    out->starve_checks = g_stats.starve_checks;
    out->starve_alerts = g_stats.starve_alerts;
    out->starve_max_wait_ms = g_stats.starve_max_wait_ms;
    out->starve_threshold_ms = g_starve_threshold_ms;
    irq_restore(ps);
}

int detector_set_starve_threshold(uint32_t ms) {
    if (ms < 100u || ms > 60000u) {
        return -1;
    }
    uint32_t ps = irq_save();
    g_starve_threshold_ms = ms;
    for (uint32_t tid = 1u; tid < MAX_TASKS; tid++) {
        g_starve_latched_instance[tid] = 0u;
        g_starve_last_alert_ms[tid] = 0u;
    }
    irq_restore(ps);
    return 0;
}
