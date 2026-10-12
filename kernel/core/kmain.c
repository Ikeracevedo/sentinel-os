#include <sentinel/kprintf.h>
#include <stdint.h>
#include "../drivers/uart.h"
#include "time.h"
#include <sentinel/task.h>
#include "sched.h"

#define NUM_WORKERS   4u
#define REPORT_EVERY  20000u

static volatile uint32_t g_turn;
static volatile uint32_t g_errors;
static volatile uint32_t g_count[NUM_WORKERS];

static __attribute__((noinline)) uint32_t deep(uint32_t n) {
    if (n == 0) {
        sched_yield();
        return 1;
    }
    uint32_t r = deep(n - 1);
    __asm__ __volatile__("" : "+r"(r));
    return r + n;
}

static void worker_step(uint32_t idx) {
    if (g_turn != idx) {
        g_errors++;
    }
    g_turn = (idx + 1u) % NUM_WORKERS;
    g_count[idx]++;
}

static void worker_task(void *arg) {
    uint32_t idx = (uint32_t)(uintptr_t)arg;
    for (;;) {
        worker_step(idx);
        sched_yield();
    }
}

static void reporter_task(void *arg) {
    (void)arg;
    for (;;) {
        worker_step(0);
        if (g_count[0] % REPORT_EVERY == 0) {
            kprintf("ciclos=%u errores_de_orden=%u (A=%u B=%u C=%u)\r\n",
                    g_count[0], g_errors, g_count[1], g_count[2], g_count[3]);
        }
        sched_yield();
    }
}

void kmain(void) {
    uart_init(115200);
    kprintf("Sentinel OS v0.1 -- build %s\n", __DATE__);

    kprintf("deep(12) = %u (esperado 79)\n", deep(12));

    timer_init(1000);
    sched_init();
    task_create("K", reporter_task, (void *)0, 3, 0);
    task_create("A", worker_task, (void *)1, 3, 0);
    task_create("B", worker_task, (void *)2, 3, 0);
    task_create("C", worker_task, (void *)3, 3, 0);
    kprintf("4 tareas creadas; kmain cede la CPU para siempre\r\n");

    task_exit();
}