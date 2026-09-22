#include <sentinel/kprintf.h>
#include <stdint.h>
#include "../arch/xtensa/cpu.h"
#include "../drivers/gpio.h"
#include "../drivers/uart.h"
#include "time.h"

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

    timer_init(1000);
    kprintf("Timer a 1000 Hz armado. Esperando 10s reales...\n");

    uint32_t start = cpu_cycles();
    uint32_t ten_sec_cycles = 10u * CPU_FREQ_HZ;
    while ((cpu_cycles() - start) < ten_sec_cycles) {
    }

    uint32_t ticks = timer_get_ticks();
    kprintf("g_ticks tras 10s = %u (esperado ~10000)\n", ticks);


    gpio_set_output(LED_PIN);

    for (;;) {
        gpio_set_high(LED_PIN);
        delay(BLINK_DELAY);
        gpio_set_low(LED_PIN);
        delay(BLINK_DELAY);
    }
}
