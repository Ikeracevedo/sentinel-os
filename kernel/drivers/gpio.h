#ifndef SENTINEL_GPIO_H
#define SENTINEL_GPIO_H

#include <stdint.h>

void gpio_set_output(uint32_t pin);
void gpio_set_high(uint32_t pin);
void gpio_set_low(uint32_t pin);

#endif
