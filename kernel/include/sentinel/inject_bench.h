#ifndef SENTINEL_INJECT_BENCH_H
#define SENTINEL_INJECT_BENCH_H

#include <stdint.h>

/* Solo existe en la imagen experimental TRACE_NOMINAL_BENCH. */
int inject_nominal_bench_start(void);
int inject_nominal_bench_stop(void);
uint32_t inject_nominal_bench_iterations(void);
uint32_t inject_nominal_bench_alloc_failures(void);

#endif
