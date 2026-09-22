#include <stdint.h>
#include "../core/time.h"

#define TIMER0_INT_BIT (1u << 6)

/* Llamado desde _UserExceptionVector cuando EXCCAUSE indica
 * una interrupcion de nivel 1 (kernel/arch/xtensa/vectors.S). */
void irq_dispatch(void) {
    uint32_t intr;
    __asm__ __volatile__("rsr %0, INTERRUPT" : "=r"(intr));

    if (intr & TIMER0_INT_BIT) {
        timer_isr();
    }
}
