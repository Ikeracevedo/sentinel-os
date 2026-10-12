#ifndef SENTINEL_TIME_H
#define SENTINEL_TIME_H

#include <stdint.h>

void timer_init(uint32_t hz);
uint32_t timer_get_ticks(void);

/* Ticks desde el arranque. Supone timer_init(1000): 1 tick = 1 ms.
 * Inline: se lee en la ruta caliente del scheduler y una llamada cuesta ciclos. */
extern volatile uint32_t g_ticks;
static inline uint32_t uptime_ms(void) {
    return g_ticks;
}
void timer_isr(void);

#endif
