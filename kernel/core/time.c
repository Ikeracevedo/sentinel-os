#include "time.h"
#include "../arch/xtensa/cpu.h"

static volatile uint32_t g_ticks = 0;
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

/* Llamado desde irq_dispatch() cuando la interrupcion es el timer0.
 * Se re-arma sumando al valor de CCOMPARE0 anterior, no a CCOUNT actual --
 * asi el periodo no se corre por el tiempo que tarda en atenderse cada tick. */
void timer_isr(void) {
    g_ticks++;

    uint32_t prev_compare;
    __asm__ __volatile__("rsr %0, CCOMPARE0" : "=r"(prev_compare));
    uint32_t next = prev_compare + g_cycles_per_tick;
    __asm__ __volatile__("wsr %0, CCOMPARE0" :: "r"(next));
}
