#include <sentinel/kprintf.h>
#include <stdint.h>
#include "../arch/xtensa/cpu.h"
#include "../drivers/gpio.h"
#include "../drivers/uart.h"
#include "time.h"
#include <sentinel/task.h>
#include "../arch/xtensa/context.h"
#include "sched.h"

#define LED_PIN      2
#define BLINK_DELAY  3000000

static void delay(volatile uint32_t count) {
    while (count--) {
        __asm__ __volatile__("nop");
    }
}

static void dummy_task(void *arg) {
    (void)arg;
    for (;;) {
    }
}

static __attribute__((noinline)) uint32_t deep(uint32_t n) {
    if (n == 0) {
        sched_yield();
        return 1;
    }
    uint32_t r = deep(n - 1);
    __asm__ __volatile__("" : "+r"(r));
    return r + n;
}


void kmain(void) {
    uart_init(115200);
    kprintf("Sentinel OS v0.1 -- build %s\n", __DATE__);

    /* TEST HU-E02-02 (temporal): creacion de tareas y marco inicial */
    int32_t tid = task_create("tarea-a", dummy_task, (void *)0x1234, 3, 0);
    tcb_t *t = task_get((uint32_t)tid);
    ctx_frame_t *f = (ctx_frame_t *)(uintptr_t)t->sp;
    kprintf("tid=%d estado=%u prio=%u nombre=%s\n",
            tid, (uint32_t)t->state, (uint32_t)t->priority, t->name);
    kprintf("sp alineado a 16: %u (esperado 1)\n", (uint32_t)((t->sp & 15u) == 0));
    kprintf("canario=%x (esperado deadbeef)\n", *t->stack_base);
    kprintf("frame: a6=%x a7=%x ps=%x (esperado a7=1234 ps=50030)\n", f->a6, f->a7, f->ps);
    kprintf("a1 inicial=%x tope pila=%x (deben coincidir)\n",
            t->sp + CTX_FRAME_SIZE, (uint32_t)(uintptr_t)t->stack_base + t->stack_size);
    kprintf("fn NULL=%d prio 9=%d pila 8KB=%d (esperado -1 -1 -1)\n",
            task_create("x", NULL, 0, 1, 0),
            task_create("x", dummy_task, 0, 9, 0),
            task_create("x", dummy_task, 0, 1, 8192));
    int32_t n = 1;
    while (task_create("x", dummy_task, 0, 7, 0) >= 0) {
        n++;
    }
    kprintf("tareas creadas = %d (esperado 16)\n", n);

    kprintf("antes de syscall\n");
    __asm__ __volatile__("syscall");
    kprintf("despues de syscall: volvio bien\n");

    kprintf("deep(12) = %u (esperado 79)\n", deep(12));

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
