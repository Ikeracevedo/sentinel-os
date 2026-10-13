#include "sched.h"
#include "time.h"
#include <sentinel/kprintf.h>
#include <sentinel/probe.h>
#include <sentinel/panic.h>
#include <sentinel/sync.h>
#include "../arch/xtensa/irq.h"
#include "../arch/xtensa/cpu.h"

#define IDLE_WINDOW_MS  1000u

static tcb_t *g_current = NULL;
static tcb_t *g_idle = NULL;

/* Contabilidad de CPU: g_mark es el CCOUNT de la ultima vez que se cargo
 * tiempo a una tarea. Se actualiza en cada tick (<= 1 ms), asi la resta de
 * 32 bits nunca da la vuelta (el contador tarda ~107 s en hacerlo). */
static uint32_t g_mark;

/* Ventana de medicion del % ocioso. */
static uint32_t g_win_start;
static uint32_t g_win_idle_base;
static uint32_t g_win_next_ms;
static uint32_t g_idle_percent;

/* Una cola FIFO circular por nivel de prioridad (0 = maxima, 7 = idle).
 * La tarea que esta corriendo NO esta en ninguna cola. */
static uint8_t g_rq[TASK_PRIORITY_LEVELS][MAX_TASKS];
static uint8_t g_rq_head[TASK_PRIORITY_LEVELS];
static uint8_t g_rq_count[TASK_PRIORITY_LEVELS];

/* El contexto de arranque (kmain) pasa a ser la tarea idle: tid 0, prioridad 7,
 * nunca termina ni se bloquea, y por eso siempre hay algo ejecutable. */
void sched_init(void) {
    g_idle = task_register_boot("idle");
    if (g_idle == NULL || g_idle->tid != 0) {
        panic("sched_init: la tarea idle debe ser el tid 0");
    }
    g_current = g_idle;

    g_mark = cpu_cycles();
    g_win_start = g_mark;
    g_win_idle_base = 0;
    g_win_next_ms = uptime_ms() + IDLE_WINDOW_MS;
    g_idle_percent = 0;
}

uint32_t cpu_idle_percent(void) {
    return g_idle_percent;
}

/* Carga a la tarea actual el tiempo transcurrido desde la ultima marca. */
static void account(void) {
    uint32_t now = cpu_cycles();
    g_current->cpu_cycles += (uint32_t)(now - g_mark);
    g_mark = now;
}

/* Cierra la ventana: % ocioso = ciclos de idle / ciclos totales de la ventana.
 * Solo aritmetica de 32 bits (no hay libgcc para dividir 64). */
static void close_idle_window(void) {
    uint32_t now = cpu_cycles();
    uint32_t total = now - g_win_start;
    uint32_t idle_now = (uint32_t)g_idle->cpu_cycles;
    uint32_t idle_delta = idle_now - g_win_idle_base;

    uint32_t unit = total / 100u;
    uint32_t pct = (unit > 0) ? (idle_delta / unit) : 0;
    g_idle_percent = (pct > 100u) ? 100u : pct;

    g_win_start = now;
    g_win_idle_base = idle_now;
    g_win_next_ms += IDLE_WINDOW_MS;
}

tcb_t *sched_current(void) {
    return g_current;
}

void sched_make_ready(tcb_t *t) {
    uint32_t p = t->priority;
    g_rq[p][(g_rq_head[p] + g_rq_count[p]) % MAX_TASKS] = t->tid;
    g_rq_count[p]++;
    t->state = TASK_READY;
    t->ready_since = uptime_ms();
}

void sched_remove_ready(tcb_t *t) {
    uint32_t p = t->priority;
    for (uint32_t i = 0; i < g_rq_count[p]; i++) {
        if (g_rq[p][(g_rq_head[p] + i) % MAX_TASKS] == t->tid) {
            for (uint32_t j = i; j + 1 < g_rq_count[p]; j++) {
                g_rq[p][(g_rq_head[p] + j) % MAX_TASKS] =
                    g_rq[p][(g_rq_head[p] + j + 1) % MAX_TASKS];
            }
            g_rq_count[p]--;
            return;
        }
    }
}

/* Nivel de la cola no vacia mas prioritaria; TASK_PRIORITY_LEVELS si no hay ninguna. */
static uint32_t best_ready_prio(void) {
    for (uint32_t p = 0; p < TASK_PRIORITY_LEVELS; p++) {
        if (g_rq_count[p] > 0) {
            return p;
        }
    }
    return TASK_PRIORITY_LEVELS;
}

/* Saca la cabeza de la cola mas prioritaria: Round-Robin dentro de cada nivel. */
static tcb_t *pick_next(void) {
    uint32_t p = best_ready_prio();
    if (p >= TASK_PRIORITY_LEVELS) {
        return NULL;
    }
    uint8_t tid = g_rq[p][g_rq_head[p]];
    g_rq_head[p] = (uint8_t)((g_rq_head[p] + 1u) % MAX_TASKS);
    g_rq_count[p]--;
    return task_get(tid);
}

/* Despierta a las tareas dormidas cuyo plazo ya vencio. Se compara la
 * diferencia con signo para que funcione aunque el contador de ticks de la vuelta. */
static void wake_sleepers(void) {
    uint32_t now = uptime_ms();
    for (uint32_t i = 0; i < MAX_TASKS; i++) {
        tcb_t *t = task_get(i);
        if (t == NULL) {
            continue;
        }
        if (t->state == TASK_SLEEPING && (int32_t)(now - t->wake_at_tick) >= 0) {
            sched_make_ready(t);
        } else if (t->state == TASK_BLOCKED && t->timed_wait &&
                   (int32_t)(now - t->wake_at_tick) >= 0) {
            sync_expire_wait(t);   /* plazo vencido: sale de la cola de espera */
        }
    }
}

/* Estadisticas de entrada a la CPU. Se mide su propio costo (CCOUNT antes y
 * despues) para comprobar el presupuesto de < 20 ciclos por cambio. */
static uint32_t g_stats_cost_total;
static uint32_t g_stats_cost_count;

static void note_switch_in(tcb_t *next) {
    uint32_t t0 = cpu_cycles();
    uint32_t wait = uptime_ms() - next->ready_since;
    if (wait > next->max_ready_wait) {
        next->max_ready_wait = wait;
    }
    next->switches_in++;
    g_stats_cost_total += cpu_cycles() - t0;
    g_stats_cost_count++;
}

uint32_t sched_stats_avg_cycles(void) {
    return g_stats_cost_count ? g_stats_cost_total / g_stats_cost_count : 0;
}

/* Canario de pila: vive en la direccion mas baja de la pila. Si cambio, la tarea
 * desbordo. idle (tid 0) usa la pila del linker y no tiene canario (stack_base NULL). */
 static void check_canary(const tcb_t *t) {
    if (t->stack_base != NULL &&
        *(volatile uint32_t *)t->stack_base != STACK_CANARY) {
        kprintf("DESBORDAMIENTO DE PILA: tarea '%s' (tid=%u)\r\n",
                t->name, (uint32_t)t->tid);
        panic("canario de pila corrupto");
    }
}



/* Guarda el marco de la tarea saliente, elige la siguiente y devuelve su marco.
 * Lo comparten yield (syscall) y la apropiacion por timer. */
static uint32_t switch_task(uint32_t frame_sp) {
    tcb_t *prev = g_current;
    prev->sp = frame_sp;
    check_canary(prev);
    if (prev->state == TASK_RUNNING) {
        sched_make_ready(prev);
    }

    tcb_t *next = pick_next();
    if (next == NULL) {
        panic("sched: no hay ninguna tarea ejecutable");
    }
    if (prev->state == TASK_TERMINATED) {
        task_free(prev);   /* seguimos en su pila hasta el rfe, pero nadie mas puede reusar la ranura antes */
    }
    note_switch_in(next);
    PROBE_SWITCH_OUT(prev, next, (uint32_t)prev->state);
    PROBE_SWITCH_IN(next, (uint32_t)next->cpu_cycles, uptime_ms() - next->ready_since);
    next->state = TASK_RUNNING;
    next->quantum_left = next->quantum_ticks;
    g_current = next;
    return next->sp;
}

static volatile int g_in_isr;
static void (*g_tick_hook)(void);

int sched_in_interrupt(void) {
    return g_in_isr;
}

void sched_set_tick_hook(void (*fn)(void)) {
    g_tick_hook = fn;
}

static uint32_t dispatch_inner(ctx_frame_t *frame) {
    uint32_t frame_sp = (uint32_t)(uintptr_t)frame;
    int is_tick = (frame->cause == EXCCAUSE_LEVEL1_INTERRUPT);

    if (is_tick) {
        irq_dispatch();
    }
    if (g_current == NULL) {
        return frame_sp;
    }
    account();
    if (is_tick) {
        if ((int32_t)(uptime_ms() - g_win_next_ms) >= 0) {
            close_idle_window();
        }
        wake_sleepers();
        if (g_tick_hook != NULL) {
            g_tick_hook();   /* aun dentro del ISR: puede hacer sem_post */
        }
        if (g_current->quantum_left > 0) {
            g_current->quantum_left--;
        }
        uint32_t best = best_ready_prio();
        int expired = (g_current->quantum_left == 0);
        if (best < g_current->priority || (expired && best == g_current->priority)) {
            return switch_task(frame_sp);
        }
        if (expired) {
            g_current->quantum_left = g_current->quantum_ticks;
        }
        return frame_sp;
    }
    return switch_task(frame_sp);
}

/* Marca el contexto de interrupcion mientras dura el despacho del tick: ahi sem_post
 * no debe ceder la CPU (el propio despachador decide el cambio al final). */
uint32_t sched_dispatch(ctx_frame_t *frame) {
    g_in_isr = (frame->cause == EXCCAUSE_LEVEL1_INTERRUPT);
    uint32_t sp = dispatch_inner(frame);
    g_in_isr = 0;
    return sp;
}

void sched_yield(void) {
    __asm__ __volatile__("syscall");
}

void sched_preempt_check(void) {
    if (g_current != NULL && best_ready_prio() < g_current->priority) {
        sched_yield();
    }
}
