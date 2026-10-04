#include <sentinel/task.h>
#include "../arch/xtensa/context.h"

#define TASK_MIN_STACK_SIZE  256u

static tcb_t   g_tasks[MAX_TASKS];
static uint8_t g_stacks[MAX_TASKS][TASK_STACK_SIZE] __attribute__((aligned(16)));
static tcb_t  *g_current = NULL;

_Static_assert(offsetof(tcb_t, sp) == 0,
               "sp debe ser el primer campo del TCB (lo asume el assembly)");

static void zero_words(void *dst, uint32_t bytes) {
    volatile uint32_t *w = (volatile uint32_t *)dst;
    for (uint32_t i = 0; i < bytes / 4u; i++) {
        w[i] = 0;
    }
}

static void copy_name(char *dst, const char *src) {
    uint32_t i = 0;
    if (src != NULL) {
        for (; i < TASK_NAME_LEN - 1u && src[i] != '\0'; i++) {
            dst[i] = src[i];
        }
    }
    dst[i] = '\0';
}

tcb_t *task_get(uint32_t tid) {
    if (tid >= MAX_TASKS || g_tasks[tid].state == TASK_UNUSED) {
        return NULL;
    }
    return &g_tasks[tid];
}

tcb_t *task_current(void) {
    return g_current;
}

tcb_t *task_alloc(void) {
    for (uint32_t i = 0; i < MAX_TASKS; i++) {
        if (g_tasks[i].state == TASK_UNUSED) {
            zero_words(&g_tasks[i], sizeof(tcb_t));
            g_tasks[i].tid = (uint8_t)i;
            g_tasks[i].state = TASK_SUSPENDED;
            return &g_tasks[i];
        }
    }
    return NULL;
}

void task_exit(void) {
    if (g_current != NULL) {
        g_current->state = TASK_TERMINATED;
    }
    for (;;) {
    }
}

/* Punto de entrada real de toda tarea: si fn retorna, la tarea termina limpia. */
static void task_trampoline(task_fn_t fn, void *arg) {
    fn(arg);
    task_exit();
}

int32_t task_create(const char *name, task_fn_t fn, void *arg,
                    uint8_t priority, uint32_t stack_size) {
    if (fn == NULL || priority >= TASK_PRIORITY_LEVELS) {
        return -1;
    }
    if (stack_size == 0) {
        stack_size = TASK_STACK_SIZE;
    }
    stack_size &= ~15u;
    if (stack_size < TASK_MIN_STACK_SIZE || stack_size > TASK_STACK_SIZE) {
        return -1;
    }

    tcb_t *t = task_alloc();
    if (t == NULL) {
        return -1;
    }

    uint8_t *base = g_stacks[t->tid];
    uint32_t top = (uint32_t)(uintptr_t)base + stack_size;

    *(uint32_t *)base = STACK_CANARY;

    ctx_frame_t *f = (ctx_frame_t *)(uintptr_t)(top - CTX_FRAME_SIZE);
    zero_words(f, CTX_FRAME_SIZE);
    f->epc1 = (uint32_t)(uintptr_t)task_trampoline;
    f->a6   = (uint32_t)(uintptr_t)fn;
    f->a7   = (uint32_t)(uintptr_t)arg;
    f->ps   = PS_UM | PS_EXCM | PS_WOE | PS_CALLINC(1);

    t->sp = (uint32_t)(uintptr_t)f;
    t->priority = priority;
    t->quantum_ticks = TASK_DEFAULT_QUANTUM_TICKS;
    t->stack_base = (uint32_t *)base;
    t->stack_size = stack_size;
    copy_name(t->name, name);
    t->cwd[0] = '/';
    t->state = TASK_READY;

    return (int32_t)t->tid;
}