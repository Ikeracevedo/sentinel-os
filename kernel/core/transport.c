#include <sentinel/transport.h>
#include <sentinel/frame.h>
#include <sentinel/trace.h>
#include <sentinel/task.h>
#include <sentinel/heap.h>
#include "../drivers/uart.h"
#include "sched.h"
#include "time.h"

/* Tracer es el unico productor de la cola TX. El ISR UART es su consumidor. */
static uint16_t g_frame_seq;
static volatile uint32_t g_frames_dropped;
static volatile uint32_t g_events_dropped;

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
    /* Los tres campos de duracion de switch se reservan hasta implementar
     * una agregacion de muestras; cero significa "no disponible". */
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
        .switch_cycles_min = 0u,
        .switch_cycles_max = 0u,
        .switch_cycles_avg = 0u,
        .task_count = tasks,
        .trace_enabled = trace_enabled,
        .cpu_idle_pct = (uint8_t)cpu_idle_percent(),
        .reserved = 0u
    };
    (void)send_frame(FRAME_STATS, (const uint8_t *)&s, sizeof(s));
}

static void tracer_task(void *arg) {
    (void)arg;
    send_hello();
    uint32_t next_heartbeat = uptime_ms() + 100u;
    uint32_t next_stats = uptime_ms() + 1000u;

    for (;;) {
        uint32_t now = uptime_ms();
        if ((int32_t)(now - next_heartbeat) >= 0) {
            (void)send_frame(FRAME_HEARTBEAT, 0, 0u);
            next_heartbeat += 100u;
        }
        if ((int32_t)(now - next_stats) >= 0) {
            send_stats();
            next_stats += 1000u;
        }

        /* El shell solo escribe en una cola de texto. Una unica tarea crea
         * todas las tramas, de modo que los bytes de LOG y EVENTS no se mezclan. */
        uint8_t log[128];
        if (uart_frame_free() >= sizeof(log) + FRAME_OVERHEAD + 100u) {
            uint16_t n = uart_log_read(log, sizeof(log));
            if (n != 0u) {
                (void)send_frame(FRAME_LOG, log, n);
            }
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
        task_sleep(10u);
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
