#include "sched.h"
#include "../arch/xtensa/irq.h"

static tcb_t *g_current = NULL;

void sched_init(void) {
    g_current = task_register_boot("kmain");
}

tcb_t *sched_current(void) {
    return g_current;
}

/* Round-Robin: primera tarea READY despues de la actual, dando la vuelta a la tabla. */
static tcb_t *pick_next(void) {
    for (uint32_t i = 1; i <= MAX_TASKS; i++) {
        tcb_t *t = task_get((g_current->tid + i) % MAX_TASKS);
        if (t != NULL && t->state == TASK_READY) {
            return t;
        }
    }
    return NULL;
}

uint32_t sched_dispatch(ctx_frame_t *frame) {
    uint32_t frame_sp = (uint32_t)(uintptr_t)frame;

    if (frame->cause == EXCCAUSE_LEVEL1_INTERRUPT) {
        irq_dispatch();
        return frame_sp;
    }

    if (g_current == NULL) {
        return frame_sp;
    }

    g_current->sp = frame_sp;
    if (g_current->state == TASK_RUNNING) {
        g_current->state = TASK_READY;
    }

    tcb_t *next = pick_next();
    if (next == NULL) {
        next = g_current;
    }

    next->state = TASK_RUNNING;
    g_current = next;
    return next->sp;
}

void sched_yield(void) {
    __asm__ __volatile__("syscall");
}