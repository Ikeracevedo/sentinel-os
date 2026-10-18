#include <stdint.h>
#include "../core/time.h"
#include "irq.h"
#ifndef SENTINEL_TRANSPORT
#define SENTINEL_TRANSPORT 0
#endif
#if SENTINEL_TRANSPORT
#include "../../drivers/uart.h"
#endif

#define TIMER0_INT_BIT (1u << 6)
#if SENTINEL_TRANSPORT
#define UART0_INT_BIT  (1u << 1)
#endif

/* Llamado desde _UserExceptionVector cuando EXCCAUSE indica
 * una interrupcion de nivel 1 (kernel/arch/xtensa/vectors.S). */
void irq_dispatch(void) {
    uint32_t intr;
    __asm__ __volatile__("rsr %0, INTERRUPT" : "=r"(intr));

    if (intr & TIMER0_INT_BIT) {
        timer_isr();
    }
    #if SENTINEL_TRANSPORT
    if (intr & UART0_INT_BIT) {
        uart_tx_isr();
    }
    #endif
}
