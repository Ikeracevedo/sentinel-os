#include "time.h"
#include "../arch/xtensa/cpu.h"

volatile uint32_t g_ticks = 0;
static uint32_t g_cycles_per_tick = 0;

void timer_init(uint32_t hz) {
    g_cycles_per_tick = CPU_FREQ_HZ / hz;

    uint32_t now;
    __asm__ __volatile__("rsr %0, CCOUNT" : "=r"(now));
    uint32_t target = now + g_cycles_per_tick;
    __asm__ __volatile__("wsr %0, CCOMPARE0" :: "r"(target));
    __asm__ __volatile__("wsr %0, INTENABLE" :: "r"(1u << 6));
}

uint32_t timer_get_ticks(void) {
    return g_ticks;
}

/* Margen minimo entre "ahora" y el proximo objetivo: si se escribe un CCOMPARE0
 * que el contador ya esta cruzando, la coincidencia se pierde hasta que CCOUNT
 * de la vuelta (~107 s a 40 MHz) y el timer queda muerto. */
#define TIMER_MIN_MARGIN_CYCLES 256

/* Llamado desde irq_dispatch() cuando la interrupcion es el timer0.
 * Se re-arma sumando al CCOMPARE0 anterior (no a CCOUNT) para que el periodo no
 * se corra. Si el ISR llego tarde (interrupciones apagadas mas de un periodo),
 * se avanza periodo a periodo contando cada tick perdido, para que ni el reloj
 * ni el timer se desincronicen. */
void timer_isr(void) {
    uint32_t compare;
    __asm__ __volatile__("rsr %0, CCOMPARE0" : "=r"(compare));
    uint32_t now = cpu_cycles();

    do {
        g_ticks++;
        compare += g_cycles_per_tick;
    } while ((int32_t)(compare - now) < TIMER_MIN_MARGIN_CYCLES);

    __asm__ __volatile__("wsr %0, CCOMPARE0" :: "r"(compare));
}
