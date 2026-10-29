#include <sentinel/transport.h>
#include <sentinel/frame.h>
#include <sentinel/trace.h>
#include <sentinel/task.h>
#include <sentinel/heap.h>
#include <sentinel/alerts.h>
#include "../drivers/uart.h"
#include "sched.h"
#include "time.h"

/* Tracer es el unico productor de la cola TX. El ISR UART es su consumidor. */
static uint16_t g_frame_seq;
static volatile uint32_t g_frames_dropped;
static volatile uint32_t g_events_dropped;

/* A carga nominal caben varios eventos por despertar sin acercarse al limite
 * de 16 por trama. Menos despertares reducen el coste del tracer y del UART. */
#define TRACER_POLL_MS 20u

typedef struct __attribute__((packed)) {
    uint64_t uptime_ticks;
    uint32_t heap_total;
    uint32_t heap_free;
    uint32_t heap_largest_free;
    uint32_t heap_peak;
    uint16_t heap_blocks;
    uint16_t heap_free_blocks;
    uint32_t events_emitted;
    uint32_t events_dropped;
    uint32_t switch_count;
    uint32_t switch_cycles_min;
    uint32_t switch_cycles_max;
    uint32_t switch_cycles_avg;
    uint8_t task_count;
    uint8_t trace_enabled;
    uint8_t cpu_idle_pct;
    uint8_t reserved;
} sys_stats_t;

_Static_assert(sizeof(sys_stats_t) == 56u, "FRAME_STATS v1 debe medir 56 B");

/* Una entrada ocupa 32 B: 16 entradas llenan exactamente el payload maximo
 * de una trama. tid=0xff representa una ranura sin tarea. */
typedef struct __attribute__((packed)) {
    uint8_t tid;
    uint8_t state;
    uint8_t priority;
    uint8_t reserved;
    uint32_t quantum_ticks;
    uint32_t cpu_cycles_low;
    uint32_t heap_bytes;
    char name[TASK_NAME_LEN];
} task_snapshot_t;

_Static_assert(sizeof(task_snapshot_t) == 32u,
               "FRAME_TASKS v1 debe usar 32 B por tarea");
_Static_assert(MAX_TASKS * sizeof(task_snapshot_t) == FRAME_MAX_PAYLOAD,
               "FRAME_TASKS debe ocupar exactamente un payload maximo");

static int send_frame(uint8_t kind, const uint8_t *payload, uint16_t length) {
    uint8_t wire[FRAME_MAX_BYTES];
    uint16_t seq = g_frame_seq++;
    uint16_t n = frame_encode(wire, sizeof(wire), kind, seq, payload, length);
    if (n == 0u || !uart_frame_write(wire, n)) {
        g_frames_dropped++;
        return 0;
    }
    return 1;
}

static void send_hello(void) {
    static const char build[] = "Sentinel " __DATE__ " " __TIME__;
    uint8_t payload[1u + sizeof(build) - 1u];
    payload[0] = trace_enabled;
    for (uint32_t i = 0; i < sizeof(build) - 1u; i++) {
        payload[i + 1u] = (uint8_t)build[i];
    }
    (void)send_frame(FRAME_HELLO, payload, sizeof(payload));
}

static void send_stats(void) {
    heap_stats_t heap;
    heap_stats(&heap);
    uint32_t switches = 0u;
    uint8_t tasks = 0u;
    for (uint32_t tid = 0; tid < MAX_TASKS; tid++) {
        tcb_t *task = task_get(tid);
        if (task != 0) {
            tasks++;
            switches += task->switches_in;
        }
    }
    sched_jitter_t jitter;
    sched_jitter_snapshot(&jitter);
    sys_stats_t s = {
        .uptime_ticks = (uint64_t)uptime_ms(),
        .heap_total = heap.total,
        .heap_free = heap.free,
        .heap_largest_free = heap.largest_free_block,
        .heap_peak = heap.peak_used,
        .heap_blocks = (uint16_t)heap.block_count,
        .heap_free_blocks = (uint16_t)heap.free_block_count,
        .events_emitted = trace_emitted(),
        .events_dropped = trace_dropped() + g_events_dropped,
        .switch_count = switches,
        .switch_cycles_min = jitter.min_us * 40u,
        .switch_cycles_max = jitter.max_us * 40u,
        .switch_cycles_avg = jitter.mean_us * 40u,
        .task_count = tasks,
        .trace_enabled = trace_enabled,
        .cpu_idle_pct = (uint8_t)cpu_idle_percent(),
        .reserved = 0u
    };
    (void)send_frame(FRAME_STATS, (const uint8_t *)&s, sizeof(s));
}

/* El dashboard necesita nombres y estados actuales; los eventos por si solos
 * solo contienen TID. Esta foto es una lectura, no controla el kernel, y se
 * publica cada segundo junto con FRAME_STATS. */
static void send_tasks(void) {
    task_snapshot_t snapshot[MAX_TASKS];
    for (uint32_t tid = 0u; tid < MAX_TASKS; tid++) {
        task_snapshot_t *out = &snapshot[tid];
        out->tid = 0xffu;
        out->state = TASK_UNUSED;
        out->priority = 0u;
        out->reserved = 0u;
        out->quantum_ticks = 0u;
        out->cpu_cycles_low = 0u;
        out->heap_bytes = 0u;
        for (uint32_t i = 0u; i < TASK_NAME_LEN; i++) {
            out->name[i] = '\0';
        }
        tcb_t *task = task_get(tid);
        if (task == 0) {
            continue;
        }
        uint32_t blocks;
        /* task_snapshot_t is a packed wire-format structure.  Do not pass
         * the address of one of its members to a function: that address may
         * be unaligned on the target.  Gather the value in aligned locals
         * and then copy it into the frame. */
        uint32_t heap_bytes = 0u;
        heap_usage_by_task(task->tid, &heap_bytes, &blocks);
        out->heap_bytes = heap_bytes;
        out->tid = task->tid;
        out->state = (uint8_t)task->state;
        out->priority = task->priority;
        out->quantum_ticks = task->quantum_ticks;
        out->cpu_cycles_low = (uint32_t)task->cpu_cycles;
        for (uint32_t i = 0u; i < TASK_NAME_LEN; i++) {
            out->name[i] = task->name[i];
        }
    }
    (void)send_frame(FRAME_TASKS, (const uint8_t *)snapshot, sizeof(snapshot));
}

static void send_alerts(uint32_t *last_sequence) {
    for (uint32_t batch = 0u; batch < 4u; batch++) {
        if (uart_frame_free() < sizeof(frame_alert_payload_t) +
                                FRAME_OVERHEAD + 100u) {
            break;
        }
        alert_t alert;
        if (!alerts_next(*last_sequence, &alert)) {
            break;
        }
        frame_alert_payload_t payload = {
            .version = FRAME_ALERT_VERSION,
            .severity = alert.severity,
            .type = alert.type,
            .reserved = 0u,
            .sequence = alert.sequence,
            .timestamp_ms = alert.timestamp_ms,
            .data = {alert.data[0], alert.data[1], alert.data[2]}
        };
        for (uint32_t i = 0u; i < FRAME_ALERT_MESSAGE_SIZE; i++) {
            payload.message[i] = alert.message[i];
        }
        if (!send_frame(FRAME_ALERT, (const uint8_t *)&payload,
                        sizeof(payload))) {
            break;
        }
        *last_sequence = alert.sequence;
    }
}

static void tracer_task(void *arg) {
    (void)arg;
    send_hello();
    uint32_t next_heartbeat = uptime_ms() + 100u;
    uint32_t next_stats = uptime_ms() + 1000u;
    uint32_t last_alert_sequence = 0u;

    for (;;) {
        uint32_t now = uptime_ms();
        if ((int32_t)(now - next_heartbeat) >= 0) {
            (void)send_frame(FRAME_HEARTBEAT, 0, 0u);
            next_heartbeat += 100u;
        }
        if ((int32_t)(now - next_stats) >= 0) {
            send_stats();
            send_tasks();
            next_stats += 1000u;
        }

        send_alerts(&last_alert_sequence);

        /* El shell solo escribe en una cola de texto. Una unica tarea crea
         * todas las tramas, de modo que los bytes de LOG y EVENTS no se mezclan.
         *
         * Un comando como `help` puede imprimir varios cientos de bytes antes
         * del siguiente tick. Vaciamos hasta cuatro fragmentos antes de la
         * traza para que el usuario nunca pierda la respuesta de su comando;
         * despues se sigue drenando EVENTS con su propio limite. */
        for (uint32_t batch = 0u; batch < 8u; batch++) {
            uint8_t log[128];
            if (uart_frame_free() < sizeof(log) + FRAME_OVERHEAD + 100u) {
                break;
            }
            uint16_t n = uart_log_read(log, sizeof(log));
            if (n == 0u || !send_frame(FRAME_LOG, log, n)) {
                break;
            }
        }

        /* La consola es interactiva: cuando aun queda una respuesta pendiente
         * no la mezclamos con miles de eventos de planificacion. En una UART
         * de 921600 baudios esta pausa dura milisegundos y evita que `help`,
         * `ps -v` o `stats` aparezcan truncados. La traza queda en su ring y
         * se drena en la siguiente vuelta cuando la cola de texto este vacia. */
        if (uart_log_pending() != 0u) {
            task_sleep(TRACER_POLL_MS);
            continue;
        }

        for (uint32_t batch = 0; batch < 4u; batch++) {
            if (uart_frame_free() < 16u * sizeof(trace_event_t) + FRAME_OVERHEAD + 100u) {
                break;
            }
            trace_event_t events[16];
            uint16_t count = 0u;
            while (count < 16u && trace_pop(&events[count])) {
                count++;
            }
            if (count == 0u) {
                break;
            }
            if (!send_frame(FRAME_EVENTS, (const uint8_t *)events,
                            (uint16_t)(count * sizeof(trace_event_t)))) {
                g_events_dropped += count;
            }
        }
        task_sleep(TRACER_POLL_MS);
    }
}

int transport_start(void) {
    g_frame_seq = 0u;
    g_frames_dropped = 0u;
    g_events_dropped = 0u;
    uart_frame_mode_enable();
    if (task_create("tracer", tracer_task, 0, 6u, 0u) < 0) {
        uart_frame_mode_disable();
        return -1;
    }
    return 0;
}

uint32_t transport_frames_dropped(void) {
    return g_frames_dropped;
}

uint32_t transport_events_dropped(void) {
    return g_events_dropped;
}
