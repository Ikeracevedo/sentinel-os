#include <sentinel/heap.h>
#include <sentinel/kprintf.h>
#include <sentinel/probe.h>
#include <sentinel/sync.h>
#include <sentinel/task.h>
#include <stdint.h>

#include "../kernel/arch/xtensa/cpu.h"

#if SENTINEL_TRACE
#include <sentinel/trace.h>
#endif

#define COST_SAMPLES 256u

static void trace_cost_task(void *arg) {
    (void)arg;
    mutex_t mutex;
    uint32_t heap_total = 0u;
    uint32_t lock_total = 0u;
    uint32_t heap_min = UINT32_MAX;
    uint32_t lock_min = UINT32_MAX;
    uint32_t errors = 0u;

    mutex_init(&mutex, "cost-test");

    /* La muestra contiene solo kmalloc+kfree: sin salida UART durante la medida. */
    for (uint32_t i = 0; i < COST_SAMPLES; i++) {
        uint32_t start = cpu_cycles();
        void *p = kmalloc(32u);
        if (p == NULL) {
            errors++;
        } else {
            kfree(p);
        }
        uint32_t elapsed = cpu_cycles() - start;
        heap_total += elapsed;
        if (elapsed < heap_min) {
            heap_min = elapsed;
        }
    }

    /* Mutex libre: prueba el camino rapido de lock/unlock en ambas variantes. */
    for (uint32_t i = 0; i < COST_SAMPLES; i++) {
        uint32_t start = cpu_cycles();
        if (mutex_lock(&mutex) != 0) {
            errors++;
        } else if (mutex_unlock(&mutex) != 0) {
            errors++;
        }
        uint32_t elapsed = cpu_cycles() - start;
        lock_total += elapsed;
        if (elapsed < lock_min) {
            lock_min = elapsed;
        }
    }

    if (mutex_destroy(&mutex) != 0) {
        errors++;
    }

#if SENTINEL_TRACE
    uint32_t dropped = trace_dropped();
#else
    uint32_t dropped = 0u;
#endif

    kprintf("TRACE_COST: trace=%u muestras=%u heap_avg=%u heap_min=%u lock_avg=%u lock_min=%u perdidos=%u errores=%u\r\n",
            (uint32_t)SENTINEL_TRACE, COST_SAMPLES,
            heap_total / COST_SAMPLES, heap_min,
            lock_total / COST_SAMPLES, lock_min, dropped, errors);
    kprintf("TRACE_COST_DONE %s\r\n",
            (errors == 0u && dropped == 0u) ? "PASS" : "FAIL");
}

int32_t __real_shell_start(void);

int32_t __wrap_shell_start(void) {
    if (task_create("trace-cost", trace_cost_task, 0, 0u, 0u) < 0) {
        kprintf("TRACE_COST_DONE FAIL: no se pudo crear la tarea\r\n");
    }
    return __real_shell_start();
}
