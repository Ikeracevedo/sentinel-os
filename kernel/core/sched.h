#ifndef SENTINEL_SCHED_H
#define SENTINEL_SCHED_H

#include <stdint.h>
#include "../arch/xtensa/context.h"

uint32_t sched_dispatch(ctx_frame_t *frame);
void sched_yield(void);

#endif