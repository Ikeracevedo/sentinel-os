#include <sentinel/graph.h>
#include <sentinel/sync.h>
#include <sentinel/kprintf.h>

#ifdef GRAPH_HOST
/* Sustitutos del reloj y las interrupciones usados por la prueba en host. */
extern uint32_t graph_host_cycles(void);
extern uint32_t graph_host_irq_save(void);
extern void graph_host_irq_restore(uint32_t saved);
#define graph_cycles() graph_host_cycles()
#define graph_irq_save() graph_host_irq_save()
#define graph_irq_restore(saved) graph_host_irq_restore(saved)
#else
#include "../arch/xtensa/cpu.h"
#define graph_cycles() cpu_cycles()
#define graph_irq_save() irq_save()
#define graph_irq_restore(saved) irq_restore(saved)
#endif

uint32_t graph_build(graph_t *out) {
    if (out == NULL) {
        return 0u;
    }
    uint32_t started = graph_cycles();
    uint32_t saved = graph_irq_save();

    /* Borrar 64 palabras de 32 bits evita 256 escrituras byte a byte.
     * La union conserva la matriz visible de 16x16 y sus 256 bytes. */
    for (uint32_t i = 0; i < (MAX_TASKS * MAX_TASKS) / sizeof(uint32_t); i++) {
        out->clear_words[i] = 0u;
    }
    for (uint32_t a = 0; a < MAX_TASKS; a++) {
        tcb_t *waiter = task_get(a);
        if (waiter == NULL || waiter->state != TASK_BLOCKED ||
            waiter->waiting_kind != WAIT_MUTEX || waiter->waiting_on == NULL) {
            continue;
        }
        mutex_t *m = (mutex_t *)waiter->waiting_on;
        tcb_t *owner = m->owner;
        if (owner == NULL || owner->tid >= MAX_TASKS || task_get(owner->tid) != owner) {
            continue;
        }
        /* E04 mantiene owner y owned[] juntos. Comprobar ambos evita una
         * arista inventada si se rompe esa invariante. */
        for (uint32_t k = 0; k < owner->owned_count && k < TASK_MAX_OWNED; k++) {
            if (owner->owned[k] == m) {
                out->edge[a][owner->tid] = 1u;
                break;
            }
        }
    }

    graph_irq_restore(saved);
    return graph_cycles() - started;
}

void graph_dump(void) {
    graph_t graph;
    uint32_t cycles = graph_build(&graph);
    uint32_t edges = 0u;
    kprintf("grafo de espera (tid -> tid):\r\n");
    for (uint32_t a = 0; a < MAX_TASKS; a++) {
        for (uint32_t b = 0; b < MAX_TASKS; b++) {
            if (graph.edge[a][b] != 0u) {
                kprintf("  %u -> %u\r\n", a, b);
                edges++;
            }
        }
    }
    if (edges == 0u) {
        kprintf("  (sin aristas)\r\n");
    }
    kprintf("aristas=%u captura=%u ciclos\r\n", edges, cycles);
}
