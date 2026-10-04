#include "sched.h"
#include "../arch/xtensa/irq.h"

uint32_t sched_dispatch(ctx_frame_t *frame) {
    if (frame->cause == EXCCAUSE_LEVEL1_INTERRUPT) {
        irq_dispatch();
    }
    return (uint32_t)(uintptr_t)frame;
}

void sched_yield(void) {
    __asm__ __volatile__("syscall");
}