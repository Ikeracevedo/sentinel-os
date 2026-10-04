#include <sentinel/task.h>

static tcb_t g_tasks[MAX_TASKS];

_Static_assert(offsetof(tcb_t, sp) == 0, "sp debe ser el primer campo del TCB (lo asume el assembly)");

tcb_t *task_get(uint32_t tid) {
    if (tid >= MAX_TASKS || g_tasks[tid].state == TASK_UNUSED) {
        return NULL;
    }
    return &g_tasks[tid];
}

/* Reserva una ranura libre. Queda SUSPENDED: existe pero aun no es planificable;
 * task_create (HU-E02-02) la completara y la marcara READY. */
tcb_t *task_alloc(void) {
    for (uint32_t i = 0; i < MAX_TASKS; i++) {
        if (g_tasks[i].state == TASK_UNUSED) {
            g_tasks[i].tid = (uint8_t)i;
            g_tasks[i].state = TASK_SUSPENDED;
            return &g_tasks[i];
        }
    }
    return NULL;
}