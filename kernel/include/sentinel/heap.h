#ifndef SENTINEL_HEAP_H
#define SENTINEL_HEAP_H

#include <stdint.h>

#define HEAP_SIZE       98304u   /* 96 KB, region estatica */
#define HEAP_HDR_SIZE   20u      /* sizeof(block_t): magic,size,next,prev + free/owner/reservado */

#define HEAP_WALK_CHUNK    32u   /* bloques por tanda con interrupciones apagadas (~65 ciclos/bloque medidos) */
#define HEAP_WALK_RETRIES  3u    /* reinicios permitidos antes de la pasada completa */

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
    uint32_t walk_restarts;       /* recorridos reiniciados porque la lista cambio a medias */
} heap_stats_t;

void heap_init(void);

/* Verifica en hardware que la region de la arena es RAM real (antes de heap_init). */
uint32_t heap_probe_region(void);
uint32_t heap_arena_address(void);

/* Devuelve un puntero alineado a 4 B, o NULL si n == 0 o no hay espacio. */
void *kmalloc(uint32_t n);
void kfree(void *p);

/* Recorre la lista (O(n)), verifica magic y canarios. Segura desde un ISR.
 * Mantiene las interrupciones apagadas como maximo HEAP_WALK_CHUNK bloques seguidos. */
void heap_stats(heap_stats_t *out);

/* 100 - (mayor_libre * 100 / libre), en %. 0 si el heap esta recien iniciado. */
uint32_t heap_fragmentation(void);
/* Lo mismo, a partir de una foto ya tomada (evita un segundo recorrido). */
uint32_t heap_frag_percent(const heap_stats_t *s);

void heap_usage_by_task(uint8_t tid, uint32_t *bytes, uint32_t *blocks);

/* Libera todos los bloques que reservo la tarea 'tid' (la usa kill). Devuelve cuantos.
 * O(n) por bloque liberado: es una operacion rara. */
uint32_t heap_free_owned(uint8_t tid);

/* Imprime detalle (primeros 32 bloques) y un mapa visual. Usa ~520 B de pila. */
void heap_dump(void);

#endif
