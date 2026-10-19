#ifndef SENTINEL_GRAPH_H
#define SENTINEL_GRAPH_H

#include <stdint.h>
#include <sentinel/task.h>

/* La matriz legible ocupa 256 B; las mascaras de 32 B permiten recorrer
 * solo las aristas presentes durante el DFS. Ambas se actualizan juntas. */
typedef struct {
    union {
        uint8_t edge[MAX_TASKS][MAX_TASKS];
        uint32_t clear_words[(MAX_TASKS * MAX_TASKS) / sizeof(uint32_t)];
    };
    uint16_t row_bits[MAX_TASKS];
} graph_t;

typedef struct {
    uint8_t count;
    uint8_t tid[MAX_TASKS]; /* orden del ciclo; el ultimo vuelve al primero */
} graph_cycle_t;

_Static_assert(sizeof(((graph_t *)0)->edge) == 256u,
               "la matriz de adyacencia debe ocupar 256 bytes");
_Static_assert(MAX_TASKS == 16u, "las mascaras de fila son de 16 bits");

static inline void graph_add_edge(graph_t *graph, uint32_t from, uint32_t to) {
    graph->edge[from][to] = 1u;
    graph->row_bits[from] |= (uint16_t)(1u << to);
}

/* Captura coherente sin mutex ni heap. Devuelve ciclos de CPU de la captura. */
uint32_t graph_build(graph_t *out);
/* DFS iterativo de tres colores; devuelve 1 y el ciclo completo, o 0. */
int graph_find_cycle(const graph_t *graph, graph_cycle_t *out);
void graph_dump(void);

#endif
