#ifndef SENTINEL_SCHED_H
#define SENTINEL_SCHED_H

#include <stdint.h>
#include <sentinel/task.h>
#include "../arch/xtensa/context.h"

void sched_init(void);
tcb_t *sched_current(void);
uint32_t sched_dispatch(ctx_frame_t *frame);
void sched_yield(void);

/* % de CPU ociosa (0-100) medido en la ultima ventana de 1 s cerrada. */
uint32_t cpu_idle_percent(void);

/* Costo promedio (ciclos) de mantener las estadisticas en cada cambio de tarea. */
uint32_t sched_stats_avg_cycles(void);

/* Colas READY. Llamar con las interrupciones apagadas (o desde el despachador). */
void sched_make_ready(tcb_t *t);
void sched_remove_ready(tcb_t *t);

/* Si hay una tarea READY de mayor prioridad que la actual, cede la CPU ya. */
void sched_preempt_check(void);

#endif