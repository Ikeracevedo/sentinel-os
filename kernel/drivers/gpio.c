#include "gpio.h"

#define GPIO_ENABLE_W1TS_REG  (*(volatile uint32_t *)0x3ff44024)
#define GPIO_OUT_W1TS_REG     (*(volatile uint32_t *)0x3ff44008)
#define GPIO_OUT_W1TC_REG     (*(volatile uint32_t *)0x3ff4400c)
#define IO_MUX_GPIO2_REG      (*(volatile uint32_t *)0x3ff49040)

#define PIN_FUNC_GPIO 2
#define MCU_SEL_S     12

/* NOTA: el offset del registro IO_MUX no es lineal por pin (tabla propia
 * de Espressif, no una fórmula). Por ahora solo soportamos GPIO2 —
 * cuando se necesiten más pines, esto se convierte en una tabla. */
void gpio_set_output(uint32_t pin) {
    if (pin == 2) {
        IO_MUX_GPIO2_REG = (PIN_FUNC_GPIO << MCU_SEL_S);
    }
    GPIO_ENABLE_W1TS_REG = (1u << pin);
}

void gpio_set_high(uint32_t pin) {
    GPIO_OUT_W1TS_REG = (1u << pin);
}

void gpio_set_low(uint32_t pin) {
    GPIO_OUT_W1TC_REG = (1u << pin);
}
