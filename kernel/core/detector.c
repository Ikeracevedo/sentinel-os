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
#include <sentinel/alerts.h>
#include "sched.h"
#include "time.h"
#include "../arch/xtensa/cpu.h"

#define DETECTOR_PERIOD_MS 500u
#define DETECTOR_PRINT_POLL_MS 20u
#define FRAG_PERIOD_MS 1000u
#define STARVE_DEFAULT_MS 500u
#define INVERSION_PERIOD_MS 100u
#define INVERSION_COOLDOWN_MS 10000u
#define JITTER_PERIOD_MS 1000u
#define JITTER_COOLDOWN_MS 10000u
#define JITTER_DEFAULT_US 50u

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
static uint32_t g_next_inversion_due;
static uint32_t g_next_jitter_due;
static uint32_t g_jitter_last_alert_ms;
static volatile uint32_t g_jitter_threshold_us = JITTER_DEFAULT_US;
static uint64_t g_inversion_prev_cpu[MAX_TASKS];
static uint32_t g_inversion_prev_instance[MAX_TASKS];
static uint32_t g_inversion_high_instance[MAX_TASKS];
static uint32_t g_inversion_owner_instance[MAX_TASKS];
static uint32_t g_inversion_started_ms[MAX_TASKS];
static uint32_t g_inversion_last_alert_ms[MAX_TASKS];
static uint32_t g_inversion_mutex_id[MAX_TASKS];

typedef struct {
    uint32_t instance;
    uint32_t wait_started_ms;
    uint32_t mutex_id;
    uint64_t cpu_cycles;
    uint8_t state;
    uint8_t priority;
    uint8_t owner_tid;
} inversion_task_t;

typedef struct {
    uint8_t high_tid;
    uint8_t low_tid;
    uint8_t medium_tid;
    uint8_t high_priority;
    uint8_t low_priority;
    uint8_t medium_priority;
    uint32_t high_instance;
    uint32_t low_instance;
    uint32_t medium_instance;
    uint32_t mutex_id;
    uint32_t duration_ms;
    uint32_t blocked_ms;
} inversion_alert_t;

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
        (void)alerts_record(now, level == 2u ? ALERT_CRIT : ALERT_WARN,
                            ALERT_FRAGMENTATION, level,
                            "Fragmentacion del heap", percent, heap.free,
                            heap.largest_free_block);
#if SENTINEL_TRACE
        trace_probe_emit(EV_ALERT, 0u, percent, heap.largest_free_block);
#endif
        kprintf("[ALERTA] FRAGMENTACION %s: frag=%u%% libre=%u B mayor=%u B\r\n",
                level == 2u ? "CRITICA" : "ADVERTENCIA", percent,
                heap.free, heap.largest_free_block);
    }
    g_frag_level = level;
}

/* Una foto coherente del mutex y las tareas evita mezclar dos cambios de
 * propietario. El analisis de CPU se hace despues, con interrupciones activas. */
static void inversion_snapshot(inversion_task_t tasks[MAX_TASKS]) {
    for (uint32_t tid = 0u; tid < MAX_TASKS; tid++) {
        tasks[tid] = (inversion_task_t){0};
        tasks[tid].owner_tid = 0xffu;
    }
    uint32_t ps = irq_save();
    for (uint32_t tid = 0u; tid < MAX_TASKS; tid++) {
        inversion_task_t *s = &tasks[tid];
        tcb_t *t = task_get(tid);
        if (t == NULL) {
            continue;
        }
        s->instance = t->instance_id;
        s->state = (uint8_t)t->state;
        s->priority = t->priority;
        s->cpu_cycles = t->cpu_cycles;
        s->wait_started_ms = t->wait_started_ms;
        if (t->state != TASK_BLOCKED || t->waiting_kind != WAIT_MUTEX ||
            t->waiting_on == NULL) {
            continue;
        }
        mutex_t *m = (mutex_t *)t->waiting_on;
        tcb_t *owner = m->owner;
        if (owner == NULL || owner->tid >= MAX_TASKS ||
            task_get(owner->tid) != owner) {
            continue;
        }
        for (uint32_t k = 0u; k < owner->owned_count && k < TASK_MAX_OWNED; k++) {
            if (owner->owned[k] == m) {
                s->owner_tid = owner->tid;
                s->mutex_id = m->id;
                break;
            }
        }
    }
    irq_restore(ps);
}

static void detector_check_inversion(void) {
    uint32_t now = uptime_ms();
    if ((int32_t)(now - g_next_inversion_due) < 0) {
        return;
    }
    g_next_inversion_due = now + INVERSION_PERIOD_MS;
    inversion_task_t tasks[MAX_TASKS];
    inversion_snapshot(tasks);
    g_stats.inversion_checks++;
    uint32_t active = 0u;

    for (uint32_t high_tid = 1u; high_tid < MAX_TASKS; high_tid++) {
        inversion_task_t *high = &tasks[high_tid];
        uint32_t low_tid = high->owner_tid;
        if (high->instance == 0u || low_tid >= MAX_TASKS ||
            tasks[low_tid].instance == 0u ||
            tasks[low_tid].state != TASK_READY ||
            tasks[low_tid].priority <= high->priority) {
            g_inversion_started_ms[high_tid] = 0u;
            continue;
        }
        inversion_task_t *low = &tasks[low_tid];
        uint32_t medium_tid = MAX_TASKS;
        uint64_t most_cpu = 0u;
        for (uint32_t tid = 1u; tid < MAX_TASKS; tid++) {
            inversion_task_t *medium = &tasks[tid];
            if (tid == high_tid || tid == low_tid || medium->instance == 0u ||
                (medium->state != TASK_READY && medium->state != TASK_RUNNING) ||
                medium->priority <= high->priority ||
                medium->priority >= low->priority ||
                g_inversion_prev_instance[tid] != medium->instance) {
                continue;
            }
            uint64_t used = medium->cpu_cycles - g_inversion_prev_cpu[tid];
            if (used > most_cpu) {
                most_cpu = used;
                medium_tid = tid;
            }
        }
        if (medium_tid >= MAX_TASKS || most_cpu < CPU_FREQ_HZ / 1000u) {
            g_inversion_started_ms[high_tid] = 0u;
            continue;
        }
        active = 1u;
        if (g_inversion_high_instance[high_tid] != high->instance ||
            g_inversion_owner_instance[high_tid] != low->instance ||
            g_inversion_mutex_id[high_tid] != high->mutex_id) {
            g_inversion_high_instance[high_tid] = high->instance;
            g_inversion_owner_instance[high_tid] = low->instance;
            g_inversion_mutex_id[high_tid] = high->mutex_id;
            g_inversion_last_alert_ms[high_tid] = now - INVERSION_COOLDOWN_MS;
            g_inversion_started_ms[high_tid] = now;
            continue;
        }
        if (g_inversion_started_ms[high_tid] == 0u) {
            g_inversion_started_ms[high_tid] = now;
            continue;
        }
        uint32_t duration = now - g_inversion_started_ms[high_tid];
        if (duration > g_stats.inversion_max_ms) {
            g_stats.inversion_max_ms = duration;
        }
        if (duration < INVERSION_PERIOD_MS ||
            now - g_inversion_last_alert_ms[high_tid] < INVERSION_COOLDOWN_MS) {
            continue;
        }
        g_inversion_last_alert_ms[high_tid] = now;
        g_stats.inversion_alerts++;
        (void)alerts_record(now, ALERT_WARN, ALERT_INVERSION,
                            (high_tid << 16) | (low_tid << 8) | medium_tid,
                            "Inversion de prioridad",
                            (high_tid << 16) | (low_tid << 8) | medium_tid,
                            duration, high->mutex_id);
        inversion_alert_t alert = {
            .high_tid = (uint8_t)high_tid, .low_tid = (uint8_t)low_tid,
            .medium_tid = (uint8_t)medium_tid,
            .high_priority = high->priority, .low_priority = low->priority,
            .medium_priority = tasks[medium_tid].priority,
            .high_instance = high->instance, .low_instance = low->instance,
            .medium_instance = tasks[medium_tid].instance,
            .mutex_id = high->mutex_id, .duration_ms = duration,
            .blocked_ms = now - high->wait_started_ms,
        };
        tcb_t *h = task_get(alert.high_tid);
        tcb_t *l = task_get(alert.low_tid);
        tcb_t *m = task_get(alert.medium_tid);
        const char *high_name = h != NULL && h->instance_id == alert.high_instance
                                ? h->name : "?";
        const char *low_name = l != NULL && l->instance_id == alert.low_instance
                               ? l->name : "?";
        const char *medium_name = m != NULL && m->instance_id == alert.medium_instance
                                  ? m->name : "?";
#if SENTINEL_TRACE
        trace_probe_emit(EV_ALERT, alert.high_tid, alert.mutex_id,
                         alert.duration_ms);
#endif
        kprintf("[ALERTA] INVERSION: alta=%s(tid %u,p%u) "
                "baja=%s(tid %u,p%u) media=%s(tid %u,p%u) "
                "mutex=%u duracion=%u ms bloqueo=%u ms\r\n",
                high_name, (uint32_t)alert.high_tid,
                (uint32_t)alert.high_priority, low_name,
                (uint32_t)alert.low_tid, (uint32_t)alert.low_priority,
                medium_name, (uint32_t)alert.medium_tid,
                (uint32_t)alert.medium_priority, alert.mutex_id,
                alert.duration_ms, alert.blocked_ms);
    }
    for (uint32_t tid = 0u; tid < MAX_TASKS; tid++) {
        g_inversion_prev_instance[tid] = tasks[tid].instance;
        g_inversion_prev_cpu[tid] = tasks[tid].cpu_cycles;
    }
    g_stats.inversion_active = active;
}

static void detector_check_jitter(void) {
    uint32_t now = uptime_ms();
    if ((int32_t)(now - g_next_jitter_due) < 0) {
        return;
    }
    g_next_jitter_due = now + JITTER_PERIOD_MS;
    sched_jitter_t jitter;
    sched_jitter_snapshot(&jitter);
    g_stats.jitter_checks++;
    if (jitter.samples < 64u ||
        jitter.stddev_us <= g_jitter_threshold_us ||
        (g_stats.jitter_alerts != 0u &&
         now - g_jitter_last_alert_ms < JITTER_COOLDOWN_MS)) {
        return;
    }
    g_jitter_last_alert_ms = now;
    g_stats.jitter_alerts++;
    (void)alerts_record(now, ALERT_WARN, ALERT_JITTER, 0u,
                        "Jitter de switch", jitter.stddev_us,
                        g_jitter_threshold_us, jitter.mean_us);
#if SENTINEL_TRACE
    trace_probe_emit(EV_ALERT, 0u, jitter.stddev_us,
                     g_jitter_threshold_us);
#endif
    kprintf("[ALERTA] JITTER: desviacion=%u us umbral=%u us "
            "media=%u us min=%u us max=%u us muestras=%u\r\n",
            jitter.stddev_us, g_jitter_threshold_us, jitter.mean_us,
            jitter.min_us, jitter.max_us, jitter.samples);
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
            (void)alerts_record(uptime_ms(), ALERT_CRIT, ALERT_DEADLOCK,
                                ((uint32_t)alert.cycle.tid[0] << 8) |
                                alert.cycle.count, "Ciclo de espera",
                                alert.cycle.tid[0], alert.cycle.count,
                                alert.mutex_id[0]);
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
            (void)alerts_record(uptime_ms(), ALERT_WARN, ALERT_STARVATION,
                                (starvation.instance_id << 8) |
                                starvation.tid,
                                "Tarea lista sin CPU", starvation.tid,
                                starvation.wait_ms, starvation.priority);
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
        detector_check_inversion();
        detector_check_jitter();
        task_sleep(DETECTOR_PRINT_POLL_MS);
    }
}

int detector_start(void) {
    g_next_frag_due = uptime_ms() + FRAG_PERIOD_MS;
    g_next_inversion_due = uptime_ms() + INVERSION_PERIOD_MS;
    g_next_jitter_due = uptime_ms() + JITTER_PERIOD_MS;
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
    out->inversion_checks = g_stats.inversion_checks;
    out->inversion_alerts = g_stats.inversion_alerts;
    out->inversion_max_ms = g_stats.inversion_max_ms;
    out->inversion_active = g_stats.inversion_active;
    out->jitter_checks = g_stats.jitter_checks;
    out->jitter_alerts = g_stats.jitter_alerts;
    out->jitter_threshold_us = g_jitter_threshold_us;
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

int detector_set_jitter_threshold(uint32_t us) {
    if (us < 1u || us > 1000u) {
        return -1;
    }
    uint32_t ps = irq_save();
    g_jitter_threshold_us = us;
    g_jitter_last_alert_ms = uptime_ms() - JITTER_COOLDOWN_MS;
    irq_restore(ps);
    return 0;
}
