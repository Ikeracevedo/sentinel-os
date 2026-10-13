#include <sentinel/task.h>
#include "../arch/xtensa/context.h"
#include <sentinel/kprintf.h>
#include <sentinel/panic.h>
#include "sched.h"
#include <sentinel/sync.h>
#include <sentinel/heap.h>
#include "time.h"
#include "../arch/xtensa/cpu.h"

#define TASK_MIN_STACK_SIZE  256u

static tcb_t   g_tasks[MAX_TASKS];
static uint8_t g_stacks[MAX_TASKS][TASK_STACK_SIZE] __attribute__((aligned(16)));

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

/* Registra como tarea el contexto que ya esta corriendo (kmain en el arranque). */
tcb_t *task_register_boot(const char *name) {
    tcb_t *t = task_alloc();
    if (t == NULL) {
        return NULL;
    }
    copy_name(t->name, name);
    t->priority = TASK_PRIORITY_LEVELS - 1u;
    t->quantum_ticks = TASK_DEFAULT_QUANTUM_TICKS;
    t->quantum_left = t->quantum_ticks;
    t->cwd[0] = '/';
    t->state = TASK_RUNNING;
    return t;
}

/* Transiciones permitidas: bit 'to' encendido en la fila 'from'. */
static const uint8_t k_allowed[7] = {
    [TASK_UNUSED]     = (1u << TASK_SUSPENDED),
    [TASK_READY]      = (1u << TASK_RUNNING) | (1u << TASK_SUSPENDED) | (1u << TASK_TERMINATED),
    [TASK_RUNNING]    = (1u << TASK_READY) | (1u << TASK_BLOCKED) | (1u << TASK_SLEEPING) |
                        (1u << TASK_SUSPENDED) | (1u << TASK_TERMINATED),
    [TASK_BLOCKED]    = (1u << TASK_READY) | (1u << TASK_TERMINATED),
    [TASK_SLEEPING]   = (1u << TASK_READY) | (1u << TASK_TERMINATED),
    [TASK_SUSPENDED]  = (1u << TASK_READY) | (1u << TASK_TERMINATED),
    [TASK_TERMINATED] = (1u << TASK_UNUSED),
};

static int transition_ok(task_state_t from, task_state_t to) {
    return (k_allowed[from] >> to) & 1u;
}

static void report_invalid(const tcb_t *t, task_state_t from, task_state_t to) {
    kprintf("transicion invalida: tarea '%s' estado %u -> %u\r\n",
            t->name, (uint32_t)from, (uint32_t)to);
}

/* Libera el TCB. El scheduler la llama al dejar una tarea TERMINATED; el
 * resto de la tabla (pila estatica) se recicla porque la ranura queda UNUSED. */
void task_free(tcb_t *t) {
    sync_release_all(t);   /* E04: sus mutex pasan (con traspaso) a quien los esperaba */
    for (uint32_t i = 0; i < TASK_MAX_OWNED; i++) {
        t->owned[i] = NULL;
    }
    t->owned_count = 0;
    t->waiting_on = NULL;
    t->waiting_kind = 0;
    t->timed_wait = 0;
    t->state = TASK_UNUSED;
}

/* Termina a OTRA tarea (comando kill de E06). Segun donde este: la saca de la cola
 * READY o de la cola de espera de un mutex/semaforo, suelta sus mutex y recupera los
 * bloques de heap que dejo reservados. */
int32_t task_kill(uint32_t tid) {
    if (tid == 0) {
        return -1;                 /* idle nunca se mata */
    }
    tcb_t *t = task_get(tid);
    if (t == NULL) {
        return -1;
    }
    if (t == sched_current()) {
        task_exit();
    }

    uint32_t saved_ps = irq_disable();
    switch (t->state) {
    case TASK_READY:
        sched_remove_ready(t);
        break;
    case TASK_BLOCKED:
        sync_cancel_wait(t);
        break;
    case TASK_SLEEPING:
    case TASK_SUSPENDED:
        break;
    default:                       /* TERMINATED: ya la libera el scheduler */
        irq_restore(saved_ps);
        return -1;
    }
    task_free(t);
    irq_restore(saved_ps);

    heap_free_owned((uint8_t)tid);
    return 0;
}

void task_exit(void) {
    tcb_t *t = sched_current();
    (void)irq_disable();
    if (t != NULL && t->tid == 0) {
        panic("la tarea idle no puede terminar");
    }
    if (t != NULL) {
        t->state = TASK_TERMINATED;
    }
    for (;;) {
        sched_yield();
    }
}

int32_t task_suspend(uint32_t tid) {
    uint32_t saved_ps = irq_disable();
    tcb_t *t = task_get(tid);
    int32_t rc = -1;
    int is_current = 0;
    int invalid = 0;
    task_state_t seen = TASK_UNUSED;

    if (t != NULL && t->tid != 0) {   /* tid 0 (idle) nunca se bloquea */
        seen = t->state;
        if (transition_ok(t->state, TASK_SUSPENDED)) {
            if (t->state == TASK_READY) {
                sched_remove_ready(t);
            } else {
                is_current = 1;       /* RUNNING: solo puede ser la actual */
            }
            t->state = TASK_SUSPENDED;
            rc = 0;
        } else {
            invalid = 1;
        }
    }
    if (is_current) {
        sched_yield();
    }
    irq_restore(saved_ps);
    if (invalid) {
        report_invalid(t, seen, TASK_SUSPENDED);
    }
    return rc;
}

int32_t task_resume(uint32_t tid) {
    uint32_t saved_ps = irq_disable();
    tcb_t *t = task_get(tid);
    int32_t rc = -1;
    int invalid = 0;
    task_state_t seen = TASK_UNUSED;

    if (t != NULL) {
        seen = t->state;
        if (t->state == TASK_SUSPENDED && transition_ok(t->state, TASK_READY)) {
            sched_make_ready(t);
            rc = 0;
        } else {
            invalid = 1;
        }
    }
    irq_restore(saved_ps);
    if (invalid) {
        report_invalid(t, seen, TASK_READY);
    }
    if (rc == 0) {
        sched_preempt_check();
    }
    return rc;
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
    t->quantum_left = t->quantum_ticks;
    t->stack_base = (uint32_t *)base;
    t->stack_size = stack_size;
    copy_name(t->name, name);
    t->cwd[0] = '/';

    /* Se publica al final: recien aqui el scheduler puede elegirla. */
    uint32_t saved_ps = irq_disable();
    sched_make_ready(t);
    irq_restore(saved_ps);
    sched_preempt_check();

    return (int32_t)t->tid;
}

/* Duerme la tarea actual. Con las interrupciones apagadas hasta ceder la CPU
 * para que el tick no pueda colarse entre "marcar SLEEPING" y el cambio. */
void task_sleep(uint32_t ms) {
    tcb_t *t = sched_current();
    if (t == NULL || t->tid == 0) {
        return;
    }
    if (ms == 0) {
        sched_yield();
        return;
    }
    uint32_t saved_ps = irq_disable();
    t->wake_at_tick = uptime_ms() + ms;
    t->state = TASK_SLEEPING;
    sched_yield();
    irq_restore(saved_ps);
}