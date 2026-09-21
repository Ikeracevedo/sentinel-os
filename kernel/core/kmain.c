#include <sentinel/kprintf.h>
#include <stdint.h>
#include "../drivers/gpio.h"
#include "../drivers/uart.h"

#define LED_PIN      2
#define BLINK_DELAY  3000000

static void delay(volatile uint32_t count) {
    while (count--) {
        __asm__ __volatile__("nop");
    }
}

void kmain(void) {
    uart_init(115200);
    kprintf("Sentinel OS v0.1 -- build %s\n", __DATE__);

    gpio_set_output(LED_PIN);

    for (;;) {
        gpio_set_high(LED_PIN);
        delay(BLINK_DELAY);
        gpio_set_low(LED_PIN);
        delay(BLINK_DELAY);
    }
}
