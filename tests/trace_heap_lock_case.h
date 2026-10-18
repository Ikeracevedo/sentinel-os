#ifndef SENTINEL_TEST_TRACE_HEAP_LOCK_CASE_H
#define SENTINEL_TEST_TRACE_HEAP_LOCK_CASE_H

#include <sentinel/heap.h>
#include <sentinel/sync.h>

static mutex_t g_trace_case_mutex;
static volatile uint32_t g_trace_case_owner_ready;

static void trace_case_owner_task(void *arg) {
    (void)arg;
    if (mutex_lock(&g_trace_case_mutex) == 0) {
        g_trace_case_owner_ready = 1u;
        task_sleep(20u);
        (void)mutex_unlock(&g_trace_case_mutex);
    }
}

static void trace_heap_lock_case_run(void) {
    trace_event_t event;
    while (trace_pop(&event) == 1) {
    }

    uint8_t requester = sched_current()->tid;
    uint32_t seen = 0u;
    uint32_t errors = 0u;
    uint32_t p_addr;
    uint32_t q_addr;

    void *p = kmalloc(37u);
    void *q = kmalloc_owner(29u, 0u);
    p_addr = (uint32_t)(uintptr_t)p;
    q_addr = (uint32_t)(uintptr_t)q;
    if (p == NULL || q == NULL || kmalloc(0u) != NULL) {
        errors++;
    }
    kfree(p);
    kfree(q);

    mutex_init(&g_trace_case_mutex, "trace-case");
    uint32_t mutex_id = g_trace_case_mutex.id;
    if (mutex_lock(&g_trace_case_mutex) != 0) {
        errors++;
    } else if (mutex_unlock(&g_trace_case_mutex) != 0) {
        errors++;
    }

    int32_t owner_tid = task_create("trace-owner", trace_case_owner_task,
                                    NULL, 1u, 0u);
    if (owner_tid < 0) {
        errors++;
    } else {
        task_sleep(1u);
        if (g_trace_case_owner_ready == 0u) {
            errors++;
        } else if (mutex_lock(&g_trace_case_mutex) != 0) {
            errors++;
        } else if (mutex_unlock(&g_trace_case_mutex) != 0) {
            errors++;
        }
        task_sleep(2u);
    }

    while (trace_pop(&event) == 1) {
        if (event.type == EV_ALLOC && event.tid == requester) {
            if (event.arg0 == 37u && event.arg1 == p_addr) seen |= 1u << 0;
            if (event.arg0 == 29u && event.arg1 == q_addr) seen |= 1u << 1;
            if (event.arg0 == 0u && event.arg1 == 0u) seen |= 1u << 2;
        } else if (event.type == EV_FREE && event.tid == requester) {
            if (event.arg0 == p_addr && event.arg1 == 44u) seen |= 1u << 3;
            if (event.arg0 == q_addr && event.arg1 == 36u) seen |= 1u << 4;
        } else if (event.type == EV_LOCK_TRY &&
                   event.tid == requester && event.arg0 == mutex_id) {
            seen |= 1u << 5;
        } else if (event.type == EV_LOCK_ACQ &&
                   event.tid == requester && event.arg0 == mutex_id) {
            if (event.arg1 == 0u) seen |= 1u << 6;
            if (event.arg1 > 0u) seen |= 1u << 7;
        } else if (event.type == EV_LOCK_REL &&
                   event.tid == requester && event.arg0 == mutex_id &&
                   event.arg1 > 0u) {
            seen |= 1u << 8;
        } else if (event.type == EV_BLOCK &&
                   event.tid == requester && event.arg0 == mutex_id &&
                   owner_tid >= 0 && event.arg1 == (uint32_t)owner_tid) {
            seen |= 1u << 9;
        } else if (event.type == EV_TASK_CREATE &&
                   owner_tid >= 0 && event.tid == (uint8_t)owner_tid &&
                   event.arg0 == 1u && event.arg1 == TASK_STACK_SIZE) {
            seen |= 1u << 10;
        } else if (event.type == EV_TASK_EXIT &&
                   owner_tid >= 0 && event.tid == (uint8_t)owner_tid &&
                   event.arg0 == 0u && event.arg1 > 0u) {
            seen |= 1u << 11;
        }
    }

    uint32_t dropped = trace_dropped();
    kprintf("TRACE_HEAP_LOCK: mascara=%x errores=%u perdidos=%u owner=%d\r\n",
            seen, errors, dropped, owner_tid);
    kprintf("TRACE_HEAP_LOCK_DONE %s\r\n",
            (seen == 0xfffu && errors == 0u && dropped == 0u)
                ? "PASS" : "FAIL");
}

#endif
