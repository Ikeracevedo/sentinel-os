#ifndef SENTINEL_CPU_H
#define SENTINEL_CPU_H

#include <stdint.h>

/* Frecuencia real de CPU medida en hardware (HU-E01-06): sin PLL, 40MHz.
 * Ver ADR-012. */
#define CPU_FREQ_HZ 40000000u

static inline uint32_t cpu_cycles(void) {
    uint32_t c;
    __asm__ __volatile__("rsr %0, ccount" : "=r"(c));
    return c;
}

static inline uint32_t cycles_to_us(uint32_t cycles) {
    return cycles / (CPU_FREQ_HZ / 1000000u);
}

/* Deshabilita interrupciones y devuelve el estado anterior (registro PS
 * completo). irq_restore() debe recibir exactamente ese valor de vuelta
 * -- asi las llamadas anidadas nunca reactivan interrupciones de mas. */
static inline uint32_t irq_disable(void) {
    uint32_t old_ps;
    __asm__ __volatile__("rsil %0, 15" : "=r"(old_ps));
    return old_ps;
}

static inline void irq_restore(uint32_t saved_ps) {
    __asm__ __volatile__("wsr %0, ps; rsync" :: "r"(saved_ps));
}

#endif
