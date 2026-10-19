#ifndef SENTINEL_GRAPH_H
#define SENTINEL_GRAPH_H

#include <stdint.h>
#include <sentinel/task.h>

/* Una fila por tarea: edge[a][b] == 1 si a espera un mutex de b. */
typedef union {
    uint8_t edge[MAX_TASKS][MAX_TASKS];
    uint32_t clear_words[(MAX_TASKS * MAX_TASKS) / sizeof(uint32_t)];
} graph_t;

_Static_assert(sizeof(graph_t) == 256u, "el grafo debe ocupar 256 bytes");

/* Captura coherente sin mutex ni heap. Devuelve ciclos de CPU de la captura. */
uint32_t graph_build(graph_t *out);
void graph_dump(void);

#endif
