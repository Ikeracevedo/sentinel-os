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
    uint32_t frag_checks;
    uint32_t frag_alerts;
    uint32_t frag_percent;
    uint32_t frag_free;
    uint32_t frag_largest;
    uint32_t starve_checks;
    uint32_t starve_alerts;
    uint32_t starve_max_wait_ms;
    uint32_t starve_threshold_ms;
} detector_stats_t;

/* Instala el chequeo de 500 ms en el tick y la tarea que imprime alertas. */
int detector_start(void);
void detector_stats(detector_stats_t *out);
/* Umbral para tareas READY (100..60000 ms); defecto 500 ms. */
int detector_set_starve_threshold(uint32_t ms);

#endif
