#ifndef SENTINEL_DETECTOR_H
#define SENTINEL_DETECTOR_H

#include <stdint.h>

typedef struct {
    uint32_t runs;
    uint32_t alerts;
    uint32_t last_cycles;
    uint32_t max_cycles;
    uint32_t max_graph_cycles;
    uint32_t max_dfs_cycles;
} detector_stats_t;

/* Instala el chequeo de 500 ms en el tick y la tarea que imprime alertas. */
int detector_start(void);
void detector_stats(detector_stats_t *out);

#endif
