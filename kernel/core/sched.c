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
static uint32_t g_default_quantum_ticks = TASK_DEFAULT_QUANTUM_TICKS;
static uint32_t g_quantum_rotations;
static uint32_t g_stats_cost_total;
static uint32_t g_stats_cost_count;
static uint32_t g_slice_start_ms;
static uint32_t g_slice_switch_base;
static uint32_t g_slice_rotation_base;

/* Contabilidad de CPU: g_mark es el CCOUNT de la ultima vez que se cargo
 * tiempo a una tarea. Se actualiza en cada tick (<= 1 ms), asi la resta de
 * 32 bits nunca da la vuelta (el contador tarda ~107 s en hacerlo). */
static uint32_t g_mark;

/* Ventana de medicion del % ocioso. */
static uint32_t g_win_start;
static uint32_t g_win_idle_base;
static uint32_t g_win_next_ms;
static uint32_t g_idle_percent;
static uint32_t g_recovered_stack_faults;

#if SENTINEL_TRACE || (defined(TRACE_OVERHEAD_BENCH) && TRACE_OVERHEAD_BENCH)
#if SENTINEL_TRACE
static uint32_t g_next_heartbeat_ms;
#endif
/* El vector toma CCOUNT al entrar y cerca del retorno a la tarea.
 * EV_SWITCH_IN se publica al entrar en la siguiente excepcion, cuando ya
 * existe la medicion. Estos cuatro simbolos los usa vectors.S. */
volatile uint32_t trace_switch_armed;
volatile uint32_t trace_switch_ready;
volatile uint32_t trace_switch_start;
volatile uint32_t trace_switch_cycles;
#define JITTER_WINDOW 64u
static uint32_t g_jitter_us[JITTER_WINDOW];
static uint32_t g_jitter_head;
static uint32_t g_jitter_count;
#if SENTINEL_TRACE
static volatile uint8_t g_switch_in_tid;
static volatile uint32_t g_switch_in_wait_ms;
#endif
#endif

/* Una cola FIFO circular por nivel de prioridad (0 = maxima, 7 = idle).
 * La tarea que esta corriendo NO esta en ninguna cola. */
static uint8_t g_rq[TASK_PRIORITY_LEVELS][MAX_TASKS];
static uint8_t g_rq_head[TASK_PRIORITY_LEVELS];
static uint8_t g_rq_count[TASK_PRIORITY_LEVELS];

/* El contexto de arranque (kmain) pasa a ser la tarea idle: tid 0, prioridad 7,
 * nunca termina ni se bloquea, y por eso siempre hay algo ejecutable. */
void sched_init(void) {
#if SENTINEL_TRACE
    trace_init();
#endif

    g_idle = task_register_boot("idle");
    if (g_idle == NULL || g_idle->tid != 0) {
        panic("sched_init: la tarea idle debe ser el tid 0");
    }
    g_current = g_idle;
    g_slice_start_ms = uptime_ms();

    g_mark = cpu_cycles();
    g_win_start = g_mark;
    g_win_idle_base = 0;
    g_win_next_ms = uptime_ms() + IDLE_WINDOW_MS;
    g_idle_percent = 0;

#if SENTINEL_TRACE || (defined(TRACE_OVERHEAD_BENCH) && TRACE_OVERHEAD_BENCH)
#if SENTINEL_TRACE
    g_next_heartbeat_ms = uptime_ms() + 100u;
#endif
    trace_switch_armed = 0u;
    trace_switch_ready = 0u;
    g_jitter_head = 0u;
    g_jitter_count = 0u;
#if SENTINEL_TRACE
    /* heap_stats recorre la arena: tomar la muestra desde una tarea, no desde el tick. */
    if (task_create("trace-heap", trace_heap_stats_task, NULL, 0u, 0u) < 0) {
        panic("sched_init: no se pudo crear trace-heap");
    }
#endif
#endif
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

uint32_t sched_quantum_ticks(void) {
    return g_default_quantum_ticks;
}

/* Rotaciones entre tareas de la misma prioridad provocadas por agotamiento
 * del turno. Tambien cuenta si despierta una tarea de mayor prioridad justo
 * en el tick final: el cambio se etiqueta PREEMPT, pero el turno si vencio. */
uint32_t sched_quantum_rotations(void) {
    uint32_t saved_ps = irq_disable();
    uint32_t count = g_quantum_rotations;
    irq_restore(saved_ps);
    return count;
}

/* Tasas desde el ultimo cambio de slice. La division usa segundos completos
 * para evitar helpers de division de 64 bits en el kernel bare metal. */
void sched_slice_stats(sched_slice_stats_t *out) {
    if (out == NULL) {
        return;
    }
    uint32_t saved_ps = irq_disable();
    uint32_t switches = g_stats_cost_count;
    uint32_t rotations = g_quantum_rotations;
    uint32_t elapsed_ms = uptime_ms() - g_slice_start_ms;
    uint32_t switch_delta = switches - g_slice_switch_base;
    uint32_t rotation_delta = rotations - g_slice_rotation_base;
    irq_restore(saved_ps);

    uint32_t seconds = elapsed_ms / 1000u;
    out->switches_total = switches;
    out->rotations_total = rotations;
    out->slice_seconds = seconds;
    out->switches_per_s_x100 = seconds == 0u ? 0u :
        (switch_delta <= UINT32_MAX / 100u ? switch_delta * 100u / seconds :
         (switch_delta / seconds) * 100u);
    out->rotations_per_s_x100 = seconds == 0u ? 0u :
        (rotation_delta <= UINT32_MAX / 100u ? rotation_delta * 100u / seconds :
         (rotation_delta / seconds) * 100u);
}

void sched_slice_reset_window(void) {
    uint32_t saved_ps = irq_disable();
    g_slice_start_ms = uptime_ms();
    g_slice_switch_base = g_stats_cost_count;
    g_slice_rotation_base = g_quantum_rotations;
    irq_restore(saved_ps);
}

/* Un tick dura 1 ms. La sección crítica impide que el timer vea una tabla
 * parcialmente actualizada; nunca se imprime ni se bloquea dentro de ella. */
int sched_set_quantum(uint32_t ms) {
    if (ms < 1u || ms > 10000u) {
        return -1;
    }
    uint32_t saved_ps = irq_disable();
    uint32_t previous = g_default_quantum_ticks;
    g_default_quantum_ticks = ms;
    g_slice_start_ms = uptime_ms();
    g_slice_switch_base = g_stats_cost_count;
    g_slice_rotation_base = g_quantum_rotations;
    for (uint32_t tid = 0; tid < MAX_TASKS; tid++) {
        tcb_t *t = task_get(tid);
        if (t == NULL) {
            continue;
        }
        t->quantum_ticks = ms;
        if (t->quantum_left > ms) {
            t->quantum_left = ms;
        }
    }
#if SENTINEL_TRACE
    trace_probe_emit(EV_SLICE_CHANGE, g_current != NULL ? g_current->tid : 0u,
                     previous, ms);
#else
    (void)previous;
#endif
    irq_restore(saved_ps);
    return 0;
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
            sync_expire_wait(t);
        }
    }
}

/* Estadisticas de entrada a la CPU. Se mide su propio costo (CCOUNT antes y
 * despues) para comprobar el presupuesto de < 20 ciclos por cambio. */

#if defined(TRACE_OVERHEAD_BENCH) && TRACE_OVERHEAD_BENCH
static sched_bench_stats_t g_bench_stats;

static uint64_t bench_square(uint32_t cycles) {
    /* Evita la multiplicacion de 64 bits: bare metal no enlaza libgcc. */
    if (cycles <= 65535u) {
        return (uint64_t)(cycles * cycles);
    }
    uint32_t lo = cycles & 0xffffu;
    uint32_t hi = cycles >> 16;
    return (uint64_t)(lo * lo) +
           ((uint64_t)(lo * hi) << 17) +
           ((uint64_t)(hi * hi) << 32);
}

void sched_bench_reset(void) {
    uint32_t saved_ps = irq_disable();
    g_bench_stats = (sched_bench_stats_t){0};
    irq_restore(saved_ps);
}

void sched_bench_snapshot(sched_bench_stats_t *out) {
    if (out == NULL) {
        return;
    }
    uint32_t saved_ps = irq_disable();
    *out = g_bench_stats;
    irq_restore(saved_ps);
}
#endif

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

#if SENTINEL_TRACE || (defined(TRACE_OVERHEAD_BENCH) && TRACE_OVERHEAD_BENCH)
/* Division entera por un divisor pequeno sin pedir __udivdi3 a libgcc. */
static uint64_t jitter_div(uint64_t value, uint32_t divisor) {
    uint64_t quotient = 0u;
    uint32_t remainder = 0u;
    for (int32_t bit = 63; bit >= 0; bit--) {
        remainder = (remainder << 1) | (uint32_t)((value >> bit) & 1u);
        if (remainder >= divisor) {
            remainder -= divisor;
            quotient |= (uint64_t)1u << bit;
        }
    }
    return quotient;
}

static uint64_t jitter_square(uint32_t value) {
    uint32_t low = value & 0xffffu;
    uint32_t high = value >> 16;
    return (uint64_t)(low * low) + ((uint64_t)(low * high) << 17) +
           ((uint64_t)(high * high) << 32);
}

static uint32_t jitter_sqrt(uint64_t value) {
    uint64_t root = 0u;
    uint64_t bit = (uint64_t)1u << 62;
    while (bit > value) {
        bit >>= 2;
    }
    while (bit != 0u) {
        if (value >= root + bit) {
            value -= root + bit;
            root = (root >> 1) + bit;
        } else {
            root >>= 1;
        }
        bit >>= 2;
    }
    return (uint32_t)root;
}
#endif

void sched_jitter_snapshot(sched_jitter_t *out) {
    if (out == NULL) {
        return;
    }
    *out = (sched_jitter_t){0};
#if SENTINEL_TRACE || (defined(TRACE_OVERHEAD_BENCH) && TRACE_OVERHEAD_BENCH)
    uint32_t samples[JITTER_WINDOW];
    uint32_t ps = irq_save();
    uint32_t count = g_jitter_count;
    for (uint32_t i = 0u; i < count; i++) {
        samples[i] = g_jitter_us[i];
    }
    irq_restore(ps);
    if (count == 0u) {
        return;
    }
    uint64_t sum = 0u;
    uint64_t squares = 0u;
    uint32_t minimum = UINT32_MAX;
    uint32_t maximum = 0u;
    for (uint32_t i = 0u; i < count; i++) {
        uint32_t sample = samples[i];
        if (sample < minimum) minimum = sample;
        if (sample > maximum) maximum = sample;
        sum += sample;
        squares += jitter_square(sample);
    }
    uint32_t mean = (uint32_t)jitter_div(sum, count);
    uint64_t mean_square = jitter_div(squares, count);
    uint64_t square_mean = jitter_square(mean);
    out->samples = count;
    out->min_us = minimum;
    out->max_us = maximum;
    out->mean_us = mean;
    out->stddev_us = jitter_sqrt(mean_square > square_mean
                                ? mean_square - square_mean : 0u);
#endif
}

uint32_t sched_recovered_stack_faults(void) {
    return g_recovered_stack_faults;
}

/* Canario de pila: vive en la direccion mas baja de la pila. Si cambio, la tarea
 * desbordo. idle (tid 0) usa la pila del linker y no tiene canario. */
static void check_canary(tcb_t *t) {
    if (t->stack_base != NULL &&
        *(volatile uint32_t *)t->stack_base != STACK_CANARY) {
        /* El escenario E09 pisa solo su canario y conserva margen para el
         * contexto del scheduler. Recuperar otros desbordamientos reales sin
         * guardar la memoria vecina seria inseguro: mantienen el panic. */
        if (t->recover_stack_fault && t->tid != 0u) {
            g_recovered_stack_faults++;
            t->state = TASK_TERMINATED;
            return;
        }
        kprintf("DESBORDAMIENTO DE PILA: tarea '%s' (tid=%u)\r\n",
                t->name, (uint32_t)t->tid);
        panic("canario de pila corrupto");
    }
}

/*
 * Guarda el marco saliente, elige la siguiente tarea y publica la nueva
 * tarea actual. reason explica por que salio la tarea anterior.
 */
static uint32_t switch_task(uint32_t frame_sp, uint32_t reason) {
#if defined(TRACE_OVERHEAD_BENCH) && TRACE_OVERHEAD_BENCH
    uint32_t bench_start = cpu_cycles();
#endif
    tcb_t *prev = g_current;

    prev->sp = frame_sp;
    check_canary(prev);
    /* Una preempcion conserva el tiempo restante de prev. Al ceder, bloquearse
     * o agotar el quantum, su siguiente turno empieza con el quantum completo.
     * Si la preempcion coincide con el ultimo tick, tambien se renueva. */
    if (reason != TRACE_SWITCH_PREEMPT || prev->quantum_left == 0u) {
        prev->quantum_left = prev->quantum_ticks;
    }
    if (prev->state == TASK_RUNNING) {
        sched_make_ready(prev);
    }

    tcb_t *next = pick_next();
    if (next == NULL) {
        panic("sched: no hay ninguna tarea ejecutable");
    }
    if (prev->state == TASK_TERMINATED) {
        task_free(prev);
    }

    note_switch_in(next);

#if SENTINEL_TRACE
    uint32_t wait_ms = uptime_ms() - next->ready_since;
    PROBE_SWITCH_OUT(prev, next, reason);
#endif

    next->state = TASK_RUNNING;
    g_current = next;

#if SENTINEL_TRACE
    g_switch_in_tid = next->tid;
    g_switch_in_wait_ms = wait_ms;
#endif
#if SENTINEL_TRACE || (defined(TRACE_OVERHEAD_BENCH) && TRACE_OVERHEAD_BENCH)
    trace_switch_armed = 1u;
#endif

#if defined(TRACE_OVERHEAD_BENCH) && TRACE_OVERHEAD_BENCH
    uint32_t bench_cycles = cpu_cycles() - bench_start;
    g_bench_stats.count++;
    g_bench_stats.sum_cycles += bench_cycles;
    if (bench_cycles > g_bench_stats.max_cycles) {
        g_bench_stats.max_cycles = bench_cycles;
    }
    g_bench_stats.sum_squares += bench_square(bench_cycles);
#endif

    return next->sp;
}

static volatile int g_in_isr;

#define MAX_TICK_HOOKS 4
static void (*g_tick_hooks[MAX_TICK_HOOKS])(void);

int sched_in_interrupt(void) {
    return g_in_isr;
}

int sched_add_tick_hook(void (*fn)(void)) {
    uint32_t ps = irq_disable();
    int rc = -1;
    for (uint32_t i = 0; i < MAX_TICK_HOOKS && rc < 0; i++) {
        if (g_tick_hooks[i] == fn) {
            rc = 0;
        } else if (g_tick_hooks[i] == NULL) {
            g_tick_hooks[i] = fn;
            rc = 0;
        }
    }
    irq_restore(ps);
    return rc;
}

void sched_remove_tick_hook(void (*fn)(void)) {
    uint32_t ps = irq_disable();
    for (uint32_t i = 0; i < MAX_TICK_HOOKS; i++) {
        if (g_tick_hooks[i] == fn) {
            g_tick_hooks[i] = NULL;
        }
    }
    irq_restore(ps);
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

#if SENTINEL_TRACE
    if (is_tick && (int32_t)(uptime_ms() - g_next_heartbeat_ms) >= 0) {
        /* El latido usa la ruta directa: los filtros de HU-E07-07 no deben callarlo. */
        trace_emit(EV_HEARTBEAT, 0u, uptime_ms(), trace_dropped());
        g_next_heartbeat_ms += 100u;
    }
#endif

    account();

    if (is_tick) {
        if ((int32_t)(uptime_ms() - g_win_next_ms) >= 0) {
            close_idle_window();
        }
        wake_sleepers();
        for (uint32_t h = 0; h < MAX_TICK_HOOKS; h++) {
            if (g_tick_hooks[h] != NULL) {
                g_tick_hooks[h]();
            }
        }
        if (g_current->quantum_left > 0) {
            g_current->quantum_left--;
        }

        uint32_t best = best_ready_prio();
        int expired = (g_current->quantum_left == 0);

        if (expired && g_rq_count[g_current->priority] != 0u) {
            g_quantum_rotations++;
        }

        if (best < g_current->priority) {
            return switch_task(frame_sp, TRACE_SWITCH_PREEMPT);
        }
        if (expired && best == g_current->priority) {
            return switch_task(frame_sp, TRACE_SWITCH_QUANTUM);
        }
        if (expired) {
            g_current->quantum_left = g_current->quantum_ticks;
        }
        return frame_sp;
    }

    uint32_t reason = TRACE_SWITCH_YIELD;
    if (g_current->state == TASK_TERMINATED) {
        reason = TRACE_SWITCH_EXIT;
    } else if (g_current->state != TASK_RUNNING) {
        reason = TRACE_SWITCH_BLOCK;
    } else if (best_ready_prio() < g_current->priority) {
        reason = TRACE_SWITCH_PREEMPT;
    }

    return switch_task(frame_sp, reason);
}

/* Marca el contexto de interrupcion mientras dura el despacho del tick:
 * ahi sem_post no debe ceder ni bloquear la CPU. */
uint32_t sched_dispatch(ctx_frame_t *frame) {
#if SENTINEL_TRACE || (defined(TRACE_OVERHEAD_BENCH) && TRACE_OVERHEAD_BENCH)
    /* La interrupcion anterior ya termino de restaurar el contexto. Publicar
     * su duracion antes de que este despacho pueda emitir otro SWITCH_OUT. */
    if (trace_switch_ready != 0u) {
        uint32_t cycles = trace_switch_cycles;
        g_jitter_us[g_jitter_head] = cycles / (CPU_FREQ_HZ / 1000000u);
        g_jitter_head = (g_jitter_head + 1u) & (JITTER_WINDOW - 1u);
        if (g_jitter_count < JITTER_WINDOW) {
            g_jitter_count++;
        }
#if defined(TRACE_OVERHEAD_BENCH) && TRACE_OVERHEAD_BENCH
        g_bench_stats.full_count++;
        g_bench_stats.full_sum_cycles += cycles;
        if (cycles > g_bench_stats.full_max_cycles) {
            g_bench_stats.full_max_cycles = cycles;
        }
        g_bench_stats.full_sum_squares += bench_square(cycles);
#endif
#if SENTINEL_TRACE
        uint8_t tid = g_switch_in_tid;
        uint32_t wait_ms = g_switch_in_wait_ms;
#endif
        trace_switch_ready = 0u;
#if SENTINEL_TRACE
        PROBE_SWITCH_IN(task_get(tid), cycles, wait_ms);
#endif
    }
    trace_switch_armed = 0u;
#endif
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
