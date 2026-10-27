#ifndef SENTINEL_SCHED_H
#define SENTINEL_SCHED_H

#include <stdint.h>
#include <sentinel/task.h>
#include "../arch/xtensa/context.h"

void sched_init(void);
tcb_t *sched_current(void);
uint32_t sched_dispatch(ctx_frame_t *frame);
void sched_yield(void);

/** Quantum global en ticks de 1 ms, aplicado a tareas actuales y futuras. */
uint32_t sched_quantum_ticks(void);
uint32_t sched_quantum_rotations(void);
typedef struct {
    uint32_t switches_total;
    uint32_t rotations_total;
    uint32_t slice_seconds;
    uint32_t switches_per_s_x100;
    uint32_t rotations_per_s_x100;
} sched_slice_stats_t;
void sched_slice_stats(sched_slice_stats_t *out);
void sched_slice_reset_window(void);
int sched_set_quantum(uint32_t ms);

/* % de CPU ociosa (0-100) medido en la ultima ventana de 1 s cerrada. */
uint32_t cpu_idle_percent(void);

/* Costo promedio (ciclos) de mantener las estadisticas en cada cambio de tarea. */
uint32_t sched_stats_avg_cycles(void);

/* Ultimos 64 cambios completos de contexto, en microsegundos. */
typedef struct {
    uint32_t samples;
    uint32_t min_us;
    uint32_t max_us;
    uint32_t mean_us;
    uint32_t stddev_us;
} sched_jitter_t;
void sched_jitter_snapshot(sched_jitter_t *out);

/* Numero de tareas de prueba retiradas al detectar su canario de pila. */
uint32_t sched_recovered_stack_faults(void);

#if defined(TRACE_OVERHEAD_BENCH) && TRACE_OVERHEAD_BENCH
/* Estadisticas acumuladas del tramo C del cambio de tarea. La prueba de
 * overhead compila exactamente esta medicion en TRACE=0 y TRACE=1. */
typedef struct {
    uint32_t count;
    uint32_t sum_cycles;
    uint32_t max_cycles;
    uint64_t sum_squares;
    uint32_t full_count;
    uint32_t full_sum_cycles;
    uint32_t full_max_cycles;
    uint64_t full_sum_squares;
} sched_bench_stats_t;
void sched_bench_snapshot(sched_bench_stats_t *out);
void sched_bench_reset(void);
#endif

/* Colas READY. Llamar con las interrupciones apagadas (o desde el despachador). */
void sched_make_ready(tcb_t *t);
void sched_remove_ready(tcb_t *t);

/* 1 mientras se atiende el tick (contexto de interrupcion): ahi no se puede ceder ni bloquear. */
int sched_in_interrupt(void);

/* Funciones que se invocan en cada tick, dentro del ISR (hasta 4; E04: sem_post desde ISR,
 * E06: leer el UART). Cada una se quita sin afectar a las demas. */
int  sched_add_tick_hook(void (*fn)(void));
void sched_remove_tick_hook(void (*fn)(void));

/* Si hay una tarea READY de mayor prioridad que la actual, cede la CPU ya. */
void sched_preempt_check(void);

#endif
