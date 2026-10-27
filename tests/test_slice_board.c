#include <sentinel/kprintf.h>
#include <sentinel/probe.h>
#include <sentinel/task.h>
#include <sentinel/trace.h>
#include <stdint.h>

#include "../kernel/core/sched.h"
#include "../kernel/core/time.h"

#define MEASURE_MS 12000u
#define SETTLE_MS   3000u
#define DRAIN_MS    1003u

static volatile uint32_t g_stop;
static volatile uint32_t g_work[2];
static uint8_t g_worker_tid[2];
static uint32_t g_switch_out;
static uint32_t g_worker_out;
static uint32_t g_worker_preempt;
static uint32_t g_worker_min_left = UINT32_MAX;

static void slice_worker(void *arg) {
    uint32_t index = (uint32_t)(uintptr_t)arg;
    while (g_stop == 0u) {
        g_work[index]++;
    }
}

static uint32_t drain_quantum_events(void) {
    uint32_t count = 0u;
    trace_event_t event;
    while (trace_pop(&event) == 1) {
        if (event.type == EV_SWITCH_OUT) {
            g_switch_out++;
            if (event.tid == g_worker_tid[0] ||
                event.tid == g_worker_tid[1]) {
                g_worker_out++;
                if (event.arg1 == TRACE_SWITCH_QUANTUM) {
                    count++;
                } else if (event.arg1 == TRACE_SWITCH_PREEMPT) {
                    g_worker_preempt++;
                }
            }
        }
    }
    return count;
}

static uint32_t measure_quantum_events(uint32_t duration_ms) {
    uint32_t start = uptime_ms();
    uint32_t count = 0u;
    for (;;) {
        uint32_t elapsed = (uint32_t)(uptime_ms() - start);
        if (elapsed >= duration_ms) {
            break;
        }
        count += drain_quantum_events();
        for (uint32_t i = 0; i < 2u; i++) {
            tcb_t *worker = task_get(g_worker_tid[i]);
            if (worker != NULL && worker->quantum_left < g_worker_min_left) {
                g_worker_min_left = worker->quantum_left;
            }
        }
        uint32_t remaining = duration_ms - elapsed;
        task_sleep(remaining < DRAIN_MS ? remaining : DRAIN_MS);
    }
    return count + drain_quantum_events();
}

static void slice_board_task(void *arg) {
    (void)arg;
    int passed = 1;

    if (sched_set_quantum(200u) != 0) {
        passed = 0;
    }
    for (uint32_t i = 0; i < 2u; i++) {
        int32_t tid = task_create("slice-hog", slice_worker,
                                  (void *)(uintptr_t)i, 6u, 0u);
        if (tid < 0) {
            passed = 0;
            g_worker_tid[i] = UINT8_MAX;
        } else {
            g_worker_tid[i] = (uint8_t)tid;
        }
    }

    uint32_t short_count = 0u;
    uint32_t long_count = 0u;
    uint32_t short_trace = 0u;
    uint32_t long_trace = 0u;
    uint32_t dropped_before = trace_dropped();
    if (passed) {
        (void)measure_quantum_events(SETTLE_MS);
        (void)drain_quantum_events();
        uint32_t rotations_before = sched_quantum_rotations();
        short_trace = measure_quantum_events(MEASURE_MS);
        short_count = sched_quantum_rotations() - rotations_before;

        if (sched_set_quantum(1500u) != 0) {
            passed = 0;
        }
        (void)measure_quantum_events(SETTLE_MS);
        (void)drain_quantum_events();
        rotations_before = sched_quantum_rotations();
        long_trace = measure_quantum_events(MEASURE_MS);
        long_count = sched_quantum_rotations() - rotations_before;
    }

    g_stop = 1u;
    task_sleep(20u);
    (void)drain_quantum_events();
    if (sched_set_quantum(TASK_DEFAULT_QUANTUM_TICKS) != 0) {
        passed = 0;
    }

    uint32_t dropped = trace_dropped() - dropped_before;
    kprintf("SLICE_BOARD_DIAG: tid=%u,%u work=%u,%u out=%u worker_out=%u preempt=%u min_left=%u\r\n",
            (uint32_t)g_worker_tid[0], (uint32_t)g_worker_tid[1],
            g_work[0], g_work[1], g_switch_out, g_worker_out,
            g_worker_preempt, g_worker_min_left);
    passed = passed && g_work[0] != 0u && g_work[1] != 0u &&
             long_count != 0u && short_count >= 5u * long_count &&
             dropped == 0u;
    kprintf("SLICE_BOARD: 200ms=%u 1500ms=%u ventana=%ums ratio_x100=%u trace=%u,%u perdidos=%u\r\n",
            short_count, long_count, MEASURE_MS,
            long_count != 0u ? 100u * short_count / long_count : 0u,
            short_trace, long_trace, dropped);
    kprintf("SLICE_BOARD_DONE %s\r\n", passed ? "PASS" : "FAIL");
}

int32_t __real_shell_start(void);

int32_t __wrap_shell_start(void) {
    if (task_create("slice-check", slice_board_task, 0, 0u, 0u) < 0) {
        kprintf("SLICE_BOARD_DONE FAIL: no se pudo crear la tarea\r\n");
    }
    return __real_shell_start();
}
