#ifndef SENTINEL_SCHED_H
#define SENTINEL_SCHED_H

#include <stdint.h>
#include <sentinel/task.h>
#include "../arch/xtensa/context.h"

void sched_init(void);
tcb_t *sched_current(void);
uint32_t sched_dispatch(ctx_frame_t *frame);
void sched_yield(void);

#endif