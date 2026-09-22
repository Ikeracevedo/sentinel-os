#ifndef SENTINEL_TIME_H
#define SENTINEL_TIME_H

#include <stdint.h>

void timer_init(uint32_t hz);
uint32_t timer_get_ticks(void);
void timer_isr(void);

#endif
