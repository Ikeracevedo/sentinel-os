#include "../drivers/gpio.h"

#define LED_PIN      2
#define BLINK_DELAY  3000000   /* sin CCOUNT todavía (HU-E01-06): a ojo */

static void delay(volatile uint32_t count) {
    while (count--) {
        __asm__ __volatile__("nop");
    }
}

void kmain(void) {
    gpio_set_output(LED_PIN);

    for (;;) {
        gpio_set_high(LED_PIN);
        delay(BLINK_DELAY);
        gpio_set_low(LED_PIN);
        delay(BLINK_DELAY);
    }
}
