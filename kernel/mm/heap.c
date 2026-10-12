/* Heap del kernel: lista de bloques contiguos con cabecera, first-fit, split y
 * coalescencia. C puro: compila con gcc nativo (-DHEAP_HOST) para probarlo en
 * el PC sin flashear (tests/test_heap.c).
 *
 * Disposicion de cada bloque dentro de la arena:
 *
 *   [ block_t (20 B) | payload ............ | canario (4 B) ]
 *                      \_______ size _______________________/
 *
 * 'size' incluye el canario, asi el canario siempre esta en payload+size-4 y
 * un bloque que no se parte (sobrante diminuto) lo mantiene al final.
 * 'next'/'prev' son OFFSETS en la arena (no punteros): la cabecera mide 20 B
 * igual en la placa (32 bits) y en el PC (64 bits).
 *
 * Costo del canario: 4 B por bloque, 1 escritura en kmalloc, 1 lectura en
 * kfree y 1 lectura por bloque usado en heap_stats. */

#include <sentinel/heap.h>
#include <sentinel/probe.h>

#ifdef HEAP_HOST

#include <stdio.h>
extern void heap_host_panic(const char *msg);
extern uint8_t heap_host_tid;

#define HEAP_PRINTF       printf
#define HEAP_PANIC(msg)   heap_host_panic(msg)
#define HEAP_LOCK()       0u
#define HEAP_UNLOCK(s)    ((void)(s))

static uint8_t cur_tid(void) { return heap_host_tid; }
static const char *task_name_of(uint8_t tid) { (void)tid; return "t"; }

#else

#include <sentinel/kprintf.h>
#include <sentinel/panic.h>
#include <sentinel/task.h>
#include "../arch/xtensa/cpu.h"
#include "../core/sched.h"

#define HEAP_PRINTF       kprintf
#define HEAP_PANIC(msg)   panic(msg)
#define HEAP_LOCK()       irq_disable()
#define HEAP_UNLOCK(s)    irq_restore(s)

/* Antes de sched_init() no hay tarea actual: el dueno es el arranque (tid 0). */
static uint8_t cur_tid(void) {
    tcb_t *t = sched_current();
    return (t != NULL) ? t->tid : 0u;
}

static const char *task_name_of(uint8_t tid) {
    tcb_t *t = task_get(tid);
    return (t != NULL) ? t->name : "?";
}

#endif

#define HEAP_MAGIC        0x5E4715A1u
#define HEAP_MAGIC_DEAD   0xDEAD5E41u   /* cabecera absorbida por una coalescencia */
#define HEAP_CANARY       0xC4A4A210u
#define HEAP_NIL          0xFFFFFFFFu
#define HEAP_ALIGN        4u
#define HEAP_MIN_SPLIT    16u           /* sobrante util minimo para partir un bloque */

#define HEAP_DUMP_CELLS      64u
#define HEAP_DUMP_CELL_SIZE  (HEAP_SIZE / HEAP_DUMP_CELLS)
#define HEAP_DUMP_MAX_LINES  48u        /* acota el tiempo con interrupciones apagadas */

typedef struct {
    uint32_t magic;
    uint32_t size;        /* payload, canario incluido */
    uint32_t next;        /* offset del bloque fisicamente siguiente, o HEAP_NIL */
    uint32_t prev;        /* offset del bloque fisicamente anterior, o HEAP_NIL */
    uint8_t  free;
    uint8_t  owner_tid;
    uint16_t reserved;
} block_t;

#define HDR  ((uint32_t)sizeof(block_t))

_Static_assert(sizeof(block_t) == HEAP_HDR_SIZE, "HEAP_HDR_SIZE no coincide con block_t");
_Static_assert(HEAP_SIZE % HEAP_ALIGN == 0, "HEAP_SIZE debe ser multiplo de la alineacion");
_Static_assert(HEAP_SIZE % HEAP_DUMP_CELLS == 0, "HEAP_SIZE debe dividirse en celdas exactas");

static uint8_t g_arena[HEAP_SIZE] __attribute__((aligned(16)));

/* Contadores incrementales: 'used' y el pico cuestan O(1) en cada operacion. */
static uint32_t g_free_bytes;
static uint32_t g_peak_used;
static uint32_t g_alloc_count;
static uint32_t g_free_count;
static uint32_t g_double_free_count;

static block_t *blk_at(uint32_t off) {
    return (block_t *)(void *)(g_arena + off);
}

static uint32_t blk_off(const block_t *b) {
    return (uint32_t)((const uint8_t *)b - g_arena);
}

static uint8_t *payload_of(block_t *b) {
    return (uint8_t *)b + HDR;
}

static uint32_t *canary_of(block_t *b) {
    return (uint32_t *)(void *)((uint8_t *)b + HDR + b->size - 4u);
}

static void check_header(const block_t *b) {
    if (b->magic != HEAP_MAGIC) {
        HEAP_PRINTF("heap: cabecera corrupta en %p (magic=%x)\r\n", (void *)b, b->magic);
        HEAP_PANIC("heap: cabecera de bloque corrupta");
    }
}

static void check_canary(block_t *b) {
    if (*canary_of(b) != HEAP_CANARY) {
        HEAP_PRINTF("heap: canario pisado en bloque %p, tam=%u, tarea '%s' (tid=%u)\r\n",
                    (void *)payload_of(b), b->size,
                    task_name_of(b->owner_tid), (uint32_t)b->owner_tid);
        HEAP_PANIC("heap: escritura fuera de los limites de un bloque");
    }
}

void heap_init(void) {
    block_t *b = blk_at(0);
    b->magic = HEAP_MAGIC;
    b->size = HEAP_SIZE - HDR;
    b->next = HEAP_NIL;
    b->prev = HEAP_NIL;
    b->free = 1;
    b->owner_tid = 0;
    b->reserved = 0;

    g_free_bytes = b->size;
    g_peak_used = HEAP_SIZE - g_free_bytes;
    g_alloc_count = 0;
    g_free_count = 0;
    g_double_free_count = 0;
}

void *kmalloc(uint32_t n) {
    if (n == 0 || n > HEAP_SIZE) {
        return NULL;   /* kmalloc(0) devuelve NULL (decision documentada) */
    }
    /* n + canario, redondeado a la alineacion */
    uint32_t need = (n + 4u + (HEAP_ALIGN - 1u)) & ~(HEAP_ALIGN - 1u);
    uint8_t tid = cur_tid();
    void *result = NULL;

    uint32_t saved = HEAP_LOCK();
    uint32_t off = 0;
    while (off != HEAP_NIL) {
        block_t *b = blk_at(off);
        check_header(b);
        if (b->free && b->size >= need) {
            uint32_t old = b->size;
            if (old > need + HDR + HEAP_MIN_SPLIT) {
                uint32_t rem_off = off + HDR + need;
                block_t *r = blk_at(rem_off);
                r->magic = HEAP_MAGIC;
                r->size = old - need - HDR;
                r->free = 1;
                r->owner_tid = 0;
                r->reserved = 0;
                r->prev = off;
                r->next = b->next;
                if (b->next != HEAP_NIL) {
                    blk_at(b->next)->prev = rem_off;
                }
                b->next = rem_off;
                b->size = need;
                g_free_bytes -= need + HDR;
            } else {
                g_free_bytes -= old;
            }
            b->free = 0;
            b->owner_tid = tid;
            *canary_of(b) = HEAP_CANARY;
            g_alloc_count++;
            if (HEAP_SIZE - g_free_bytes > g_peak_used) {
                g_peak_used = HEAP_SIZE - g_free_bytes;
            }
            result = payload_of(b);
            break;
        }
        off = b->next;
    }
    HEAP_UNLOCK(saved);

    PROBE_ALLOC(result, n, tid);
    return result;
}

void kfree(void *p) {
    if (p == NULL) {
        return;
    }
    uint8_t *bp = (uint8_t *)p;
    if (bp < g_arena + HDR || bp >= g_arena + HEAP_SIZE ||
        ((uintptr_t)bp & (HEAP_ALIGN - 1u)) != 0) {
        HEAP_PRINTF("kfree: puntero %p fuera del heap o desalineado\r\n", p);
        HEAP_PANIC("kfree: puntero invalido");
        return;
    }
    block_t *b = (block_t *)(void *)(bp - HDR);

    uint32_t saved = HEAP_LOCK();

    /* Doble free: el bloque sigue libre, o su cabecera fue absorbida por una fusion. */
    if (b->magic == HEAP_MAGIC_DEAD || (b->magic == HEAP_MAGIC && b->free)) {
        g_double_free_count++;
        HEAP_UNLOCK(saved);
        HEAP_PRINTF("kfree: doble free detectado en %p (ignorado)\r\n", p);
        return;
    }
    check_header(b);
    check_canary(b);

    uint8_t owner = b->owner_tid;
    b->free = 1;
    b->owner_tid = 0;
    g_free_bytes += b->size;
    g_free_count++;

    /* Coalescencia hacia adelante: absorbo al vecino siguiente si esta libre. */
    if (b->next != HEAP_NIL) {
        block_t *nx = blk_at(b->next);
        check_header(nx);
        if (nx->free) {
            b->size += HDR + nx->size;
            g_free_bytes += HDR;          /* la cabecera absorbida pasa a ser payload libre */
            b->next = nx->next;
            if (nx->next != HEAP_NIL) {
                blk_at(nx->next)->prev = blk_off(b);
            }
            nx->magic = HEAP_MAGIC_DEAD;
        }
    }

    /* Coalescencia hacia atras: el vecino anterior me absorbe si esta libre. */
    if (b->prev != HEAP_NIL) {
        block_t *pv = blk_at(b->prev);
        check_header(pv);
        if (pv->free) {
            pv->size += HDR + b->size;
            g_free_bytes += HDR;
            pv->next = b->next;
            if (b->next != HEAP_NIL) {
                blk_at(b->next)->prev = blk_off(pv);
            }
            b->magic = HEAP_MAGIC_DEAD;
        }
    }
    HEAP_UNLOCK(saved);

    PROBE_FREE(p, owner);
}

void heap_stats(heap_stats_t *out) {
    uint32_t blocks = 0;
    uint32_t free_blocks = 0;
    uint32_t free_sum = 0;
    uint32_t largest = 0;

    uint32_t saved = HEAP_LOCK();
    uint32_t off = 0;
    while (off != HEAP_NIL) {
        block_t *b = blk_at(off);
        check_header(b);
        blocks++;
        if (b->free) {
            free_blocks++;
            free_sum += b->size;
            if (b->size > largest) {
                largest = b->size;
            }
        } else {
            check_canary(b);
        }
        off = b->next;
    }
    out->total = HEAP_SIZE;
    out->free = free_sum;
    out->used = HEAP_SIZE - free_sum;
    out->largest_free_block = largest;
    out->block_count = blocks;
    out->free_block_count = free_blocks;
    out->peak_used = g_peak_used;
    out->alloc_count = g_alloc_count;
    out->free_count = g_free_count;
    out->double_free_count = g_double_free_count;
    HEAP_UNLOCK(saved);
}

static uint32_t frag_of(const heap_stats_t *s) {
    if (s->free == 0) {
        return 0;
    }
    return 100u - (s->largest_free_block * 100u) / s->free;
}

uint32_t heap_fragmentation(void) {
    heap_stats_t s;
    heap_stats(&s);
    return frag_of(&s);
}

void heap_usage_by_task(uint8_t tid, uint32_t *bytes, uint32_t *blocks) {
    uint32_t nbytes = 0;
    uint32_t nblocks = 0;

    uint32_t saved = HEAP_LOCK();
    uint32_t off = 0;
    while (off != HEAP_NIL) {
        block_t *b = blk_at(off);
        check_header(b);
        if (!b->free && b->owner_tid == tid) {
            nbytes += b->size;
            nblocks++;
        }
        off = b->next;
    }
    HEAP_UNLOCK(saved);

    if (bytes != NULL) {
        *bytes = nbytes;
    }
    if (blocks != NULL) {
        *blocks = nblocks;
    }
}

/* Suma a cada celda del mapa los bytes de [start, end) que caen en ella. */
static void mark_cells(uint32_t start, uint32_t end, uint32_t *cells) {
    while (start < end) {
        uint32_t c = start / HEAP_DUMP_CELL_SIZE;
        uint32_t cell_end = (c + 1u) * HEAP_DUMP_CELL_SIZE;
        uint32_t stop = (end < cell_end) ? end : cell_end;
        cells[c] += stop - start;
        start = stop;
    }
}

void heap_dump(void) {
    heap_stats_t s;
    heap_stats(&s);

    uint32_t cells[HEAP_DUMP_CELLS];
    for (uint32_t i = 0; i < HEAP_DUMP_CELLS; i++) {
        cells[i] = 0;
    }

    HEAP_PRINTF("--- mapa del heap: total=%u usado=%u libre=%u frag=%u%% ---\r\n",
                s.total, s.used, s.free, frag_of(&s));

    uint32_t saved = HEAP_LOCK();
    uint32_t n = 0;
    uint32_t off = 0;
    while (off != HEAP_NIL) {
        block_t *b = blk_at(off);
        check_header(b);
        if (n < HEAP_DUMP_MAX_LINES) {
            HEAP_PRINTF("%p tam=%u %s %s\r\n", (void *)payload_of(b), b->size,
                        b->free ? "LIBRE" : "USADO",
                        b->free ? "-" : task_name_of(b->owner_tid));
        }
        n++;
        if (!b->free) {
            mark_cells(off, off + HDR + b->size, cells);
        }
        off = b->next;
    }
    if (n > HEAP_DUMP_MAX_LINES) {
        HEAP_PRINTF("... (+%u bloques mas)\r\n", n - HEAP_DUMP_MAX_LINES);
    }

    char line[HEAP_DUMP_CELLS + 1u];
    for (uint32_t i = 0; i < HEAP_DUMP_CELLS; i++) {
        if (cells[i] == 0) {
            line[i] = '.';
        } else if (cells[i] * 4u >= HEAP_DUMP_CELL_SIZE * 3u) {
            line[i] = '#';
        } else {
            line[i] = '+';
        }
    }
    line[HEAP_DUMP_CELLS] = '\0';
    HEAP_PRINTF("[%s]\r\n", line);
    HEAP_PRINTF("# usado  + mixto  . libre   (1 celda = %u B)\r\n", HEAP_DUMP_CELL_SIZE);
    HEAP_UNLOCK(saved);
}
