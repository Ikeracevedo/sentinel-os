#include <sentinel/kprintf.h>
#include <stdint.h>
#include "../arch/xtensa/cpu.h"
#include "../drivers/gpio.h"
#include "../drivers/uart.h"

#define LED_PIN      2
#define BLINK_DELAY  3000000
#define TIMER0_INT_BIT (1u << 6)

static volatile uint32_t g_timer0_isr_count = 0;

/* Llamada desde _UserExceptionVector (kernel/arch/xtensa/vectors.S)
 * cuando EXCCAUSE indica una interrupcion de nivel 1. */
void irq_dispatch(void) {
    uint32_t intr;
    __asm__ __volatile__("rsr %0, INTERRUPT" : "=r"(intr));

    if (intr & TIMER0_INT_BIT) {
        g_timer0_isr_count++;
        uint32_t now;
        __asm__ __volatile__("rsr %0, CCOUNT" : "=r"(now));
        uint32_t next = now + 100000000u; /* rearmar lejos: prueba de una sola vez */
        __asm__ __volatile__("wsr %0, CCOMPARE0" :: "r"(next));
    }
}

static void delay(volatile uint32_t count) {
    while (count--) {
        __asm__ __volatile__("nop");
    }
}

void kmain(void) {
    uart_init(115200);
    kprintf("Sentinel OS v0.1 -- build %s\n", __DATE__);

    /* Probar que irq_disable/irq_restore se pueden anidar sin romper nada */
    uint32_t ps1 = irq_disable();
    uint32_t ps2 = irq_disable();
    irq_restore(ps2);
    irq_restore(ps1);
    kprintf("irq_disable/irq_restore anidados: OK\n");

    /* Armar el timer interno (CCOMPARE0) para disparar en ~100ms y habilitar
     * su interrupcion (bit 6 de INTENABLE) -- prueba de HU-E01-07 */
    uint32_t target;
    __asm__ __volatile__("rsr %0, CCOUNT" : "=r"(target));
    target += CPU_FREQ_HZ / 10u;   /* ~100ms a 40MHz */
    __asm__ __volatile__("wsr %0, CCOMPARE0" :: "r"(target));
    __asm__ __volatile__("wsr %0, INTENABLE" :: "r"(TIMER0_INT_BIT));

    kprintf("Esperando interrupcion de prueba...\n");
    while (g_timer0_isr_count == 0) {
    }
    kprintf("Interrupcion recibida. contador=%u\n", g_timer0_isr_count);

    gpio_set_output(LED_PIN);

    for (;;) {
        gpio_set_high(LED_PIN);
        delay(BLINK_DELAY);
        gpio_set_low(LED_PIN);
        delay(BLINK_DELAY);
    }
}
