#ifndef SENTINEL_HEAP_H
#define SENTINEL_HEAP_H

#include <stdint.h>

#define HEAP_SIZE      98304u   /* 96 KB, region estatica */
#define HEAP_HDR_SIZE  20u      /* sizeof(block_t): magic,size,next,prev + free/owner/reservado */

typedef struct {
    uint32_t total;               /* HEAP_SIZE */
    uint32_t used;                /* total - free: payloads usados + TODAS las cabeceras */
    uint32_t free;                /* suma de payloads de bloques libres */
    uint32_t largest_free_block;
    uint32_t block_count;
    uint32_t free_block_count;
    uint32_t peak_used;           /* marca de agua de 'used' */
    uint32_t alloc_count;
    uint32_t free_count;
    uint32_t double_free_count;   /* dobles kfree detectados (reportados, no fatales) */
} heap_stats_t;

void heap_init(void);

/* Devuelve un puntero alineado a 4 B, o NULL si n == 0 o no hay espacio. */
void *kmalloc(uint32_t n);
void kfree(void *p);

/* Recorre la lista (O(n)), verifica magic y canarios. Segura desde un ISR. */
void heap_stats(heap_stats_t *out);

/* 100 - (mayor_libre * 100 / libre), en %. 0 si el heap esta recien iniciado. */
uint32_t heap_fragmentation(void);

void heap_usage_by_task(uint8_t tid, uint32_t *bytes, uint32_t *blocks);
void heap_dump(void);

#endif
