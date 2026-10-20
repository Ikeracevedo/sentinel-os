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
    uint32_t saved = graph_irq_save();
    uint32_t started = graph_cycles();

    /* Borrar 64 palabras de 32 bits evita 256 escrituras byte a byte.
     * La union conserva la matriz visible de 16x16 y sus 256 bytes. */
    for (uint32_t i = 0; i < (MAX_TASKS * MAX_TASKS) / sizeof(uint32_t); i++) {
        out->clear_words[i] = 0u;
    }
    for (uint32_t i = 0; i < MAX_TASKS; i++) {
        out->row_bits[i] = 0u;
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
                graph_add_edge(out, a, owner->tid);
                break;
            }
        }
    }

    uint32_t capture_cycles = graph_cycles() - started;
    graph_irq_restore(saved);
    return capture_cycles;
}

int graph_find_cycle(const graph_t *graph, graph_cycle_t *out) {
    if (graph == NULL || out == NULL) {
        return 0;
    }
    out->count = 0u;
    uint8_t color[MAX_TASKS] = {0}; /* 0 blanco, 1 gris, 2 negro */
    uint16_t remaining[MAX_TASKS]; /* aristas aun no recorridas por fila */
    uint8_t stack[MAX_TASKS];
    uint32_t depth = 0u;
    for (uint32_t v = 0; v < MAX_TASKS; v++) {
        remaining[v] = graph->row_bits[v];
    }

    for (uint32_t start = 0; start < MAX_TASKS; start++) {
        if (color[start] != 0u) {
            continue;
        }
        stack[depth++] = (uint8_t)start;
        color[start] = 1u;
        while (depth > 0u) {
            uint8_t v = stack[depth - 1u];
            uint16_t bits = remaining[v];
            if (bits == 0u) {
                color[v] = 2u;
                depth--;
                continue;
            }
            uint8_t w = (uint8_t)__builtin_ctz((unsigned)bits);
            remaining[v] = (uint16_t)(bits & (uint16_t)(bits - 1u));
            if (color[w] == 1u) {
                uint32_t first = 0u;
                while (stack[first] != w) {
                    first++;
                }
                out->count = (uint8_t)(depth - first);
                for (uint32_t i = 0; i < out->count; i++) {
                    out->tid[i] = stack[first + i];
                }
                return 1;
            }
            if (color[w] == 0u) {
                stack[depth++] = w;
                color[w] = 1u;
            }
        }
    }
    return 0;
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
    graph_cycle_t cycle;
    if (graph_find_cycle(&graph, &cycle)) {
        kprintf("ciclo: ");
        for (uint32_t i = 0; i < cycle.count; i++) {
            kprintf("%u -> ", (uint32_t)cycle.tid[i]);
        }
        kprintf("%u\r\n", (uint32_t)cycle.tid[0]);
    } else {
        kprintf("sin ciclos\r\n");
    }
    kprintf("aristas=%u captura=%u ciclos\r\n", edges, cycles);
}
