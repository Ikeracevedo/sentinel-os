/* Tests del allocator en el PC: heap.c se compila sin modificar (-DHEAP_HOST).
 * Uso: make test */

#include <setjmp.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sentinel/heap.h>

/* ---- ganchos que heap.c espera en modo host ---- */
uint8_t heap_host_tid = 0;
static jmp_buf g_jmp;
static int g_expect_panic;

void heap_host_panic(const char *msg) {
    if (g_expect_panic) {
        longjmp(g_jmp, 1);
    }
    printf("PANIC inesperado: %s\n", msg);
    exit(2);
}

/* ---- mini framework ---- */
static int g_total;
static int g_failed;

#define CHECK(cond, name) do {                                   \
        g_total++;                                               \
        if (cond) {                                              \
            printf("[PASS] %s\n", name);                         \
        } else {                                                 \
            g_failed++;                                          \
            printf("[FAIL] %s  (%s:%d)\n", name, __FILE__, __LINE__); \
        }                                                        \
    } while (0)

/* Ejecuta fn(arg) y dice si provoco un panic. */
static int panics(void (*fn)(void *), void *arg) {
    volatile int hit = 0;
    g_expect_panic = 1;
    if (setjmp(g_jmp) == 0) {
        fn(arg);
    } else {
        hit = 1;
    }
    g_expect_panic = 0;
    return hit;
}

static void do_free(void *p)  { kfree(p); }
static void do_stats(void *p) { heap_stats((heap_stats_t *)p); }

static heap_stats_t stats(void) {
    heap_stats_t s;
    heap_stats(&s);
    return s;
}

static int same_layout(heap_stats_t a, heap_stats_t b) {
    return a.total == b.total && a.used == b.used && a.free == b.free &&
           a.largest_free_block == b.largest_free_block &&
           a.block_count == b.block_count && a.free_block_count == b.free_block_count;
}

/* ---- casos ---- */

static void test_init(void) {
    heap_init();
    heap_stats_t s = stats();
    CHECK(s.total == HEAP_SIZE, "init: total = 96 KB");
    CHECK(s.block_count == 1 && s.free_block_count == 1, "init: un unico bloque libre");
    CHECK(s.largest_free_block == s.free, "init: el mayor libre es todo lo libre");
    CHECK(s.free == HEAP_SIZE - HEAP_HDR_SIZE, "init: libre = total - 1 cabecera");
    CHECK(heap_fragmentation() == 0, "init: fragmentacion 0%");
}

static void test_alloc_basic(void) {
    heap_init();
    uint8_t *a = kmalloc(100);
    uint8_t *b = kmalloc(100);
    CHECK(a != NULL && b != NULL, "alloc: devuelve punteros validos");
    CHECK(((uintptr_t)a & 3u) == 0 && ((uintptr_t)b & 3u) == 0, "alloc: alineado a 4 B");

    memset(a, 0xAA, 100);
    memset(b, 0x55, 100);
    int intact = 1;
    for (int i = 0; i < 100; i++) {
        if (a[i] != 0xAA || b[i] != 0x55) {
            intact = 0;
        }
    }
    CHECK(intact, "alloc: dos bloques no se solapan");
    CHECK(stats().block_count == 3, "alloc: 2 usados + 1 libre sobrante (split)");
    CHECK(kmalloc(0) == NULL, "alloc: kmalloc(0) = NULL");

    int aligned = 1;
    for (uint32_t n = 1; n <= 9; n++) {
        if (((uintptr_t)kmalloc(n) & 3u) != 0) {
            aligned = 0;
        }
    }
    CHECK(aligned, "alloc: tamanos impares siguen alineados a 4 B");
}

static void test_exhaustion(void) {
    heap_init();
    heap_stats_t before = stats();
    CHECK(kmalloc(HEAP_SIZE) == NULL, "agotamiento: pedir todo el heap = NULL");
    CHECK(kmalloc(HEAP_SIZE * 2u) == NULL, "agotamiento: pedir mas que el heap = NULL");
    CHECK(same_layout(before, stats()), "agotamiento: el fallo no corrompe el heap");

    uint32_t count = 0;
    while (kmalloc(1000) != NULL) {
        count++;
    }
    CHECK(count > 90, "agotamiento: se llena el heap con bloques de 1000 B");
    CHECK(kmalloc(1000) == NULL, "agotamiento: heap lleno devuelve NULL");
    CHECK(stats().alloc_count == count, "agotamiento: contador de allocs exacto");
}

static void test_free_and_coalesce(void) {
    heap_init();
    heap_stats_t initial = stats();

    void *p = kmalloc(64);
    kfree(p);
    CHECK(same_layout(initial, stats()), "free: alloc+free vuelve al estado inicial");
    kfree(NULL);
    CHECK(same_layout(initial, stats()), "free: kfree(NULL) no hace nada");

    /* hacia adelante: libero b (vecino de a) y luego a -> a absorbe a b */
    heap_init();
    void *a = kmalloc(64);
    void *b = kmalloc(64);
    void *guard = kmalloc(64);
    kfree(b);
    kfree(a);
    CHECK(stats().block_count == 3, "coalesce adelante: a+b fusionados (guardia + libre final)");

    /* hacia atras: libero a y luego b -> b es absorbido por a */
    heap_init();
    a = kmalloc(64);
    b = kmalloc(64);
    guard = kmalloc(64);
    kfree(a);
    kfree(b);
    CHECK(stats().block_count == 3, "coalesce atras: a+b fusionados (guardia + libre final)");

    /* tres contiguos, orden a-c-b: el ultimo libera y fusiona en ambos sentidos */
    heap_init();
    a = kmalloc(64);
    b = kmalloc(64);
    void *c = kmalloc(64);
    guard = kmalloc(64);
    kfree(a);
    kfree(c);
    kfree(b);
    heap_stats_t s = stats();
    CHECK(s.block_count == 3 && s.free_block_count == 2, "coalesce: 3 contiguos liberados = 1 bloque");
    kfree(guard);
    CHECK(same_layout(initial, stats()), "coalesce: todo liberado = estado inicial exacto");
}

static void test_thousand(void) {
    heap_init();
    heap_stats_t initial = stats();
    static void *ptrs[1000];

    int ok = 1;
    for (int i = 0; i < 1000; i++) {
        ptrs[i] = kmalloc(32);
        if (ptrs[i] == NULL) {
            ok = 0;
        }
    }
    CHECK(ok, "1000 allocs: todos servidos");
    for (int i = 0; i < 1000; i++) {
        kfree(ptrs[i]);
    }
    CHECK(same_layout(initial, stats()), "1000 allocs + 1000 frees: estado inicial exacto");

    for (int i = 0; i < 1000; i++) {
        ptrs[i] = kmalloc(32);
    }
    for (int i = 0; i < 1000; i += 2) {
        kfree(ptrs[i]);
    }
    for (int i = 1; i < 1000; i += 2) {
        kfree(ptrs[i]);
    }
    CHECK(same_layout(initial, stats()), "1000 allocs liberados pares/impares: estado inicial exacto");
}

static void test_fragmentation(void) {
    heap_init();
    static void *ptrs[2000];
    int n = 0;
    while (n < 2000 && (ptrs[n] = kmalloc(100)) != NULL) {
        n++;
    }
    CHECK(heap_fragmentation() == 0 || stats().free < 200u, "frag: heap lleno casi sin libre");

    for (int i = 0; i < n; i += 2) {
        kfree(ptrs[i]);
    }
    uint32_t frag = heap_fragmentation();
    heap_stats_t s = stats();
    printf("       frag intercalada = %u%%, libre=%u, mayor libre=%u\n",
           frag, s.free, s.largest_free_block);
    CHECK(frag > 40, "frag: sube de forma medible tras liberar bloques alternos");
    CHECK(kmalloc(s.free / 2u) == NULL, "frag: con mitad libre no hay bloque para la mitad (la tesis)");

    for (int i = 1; i < n; i += 2) {
        kfree(ptrs[i]);
    }
    CHECK(heap_fragmentation() == 0, "frag: vuelve a 0% al liberar todo");
}

static void test_double_free_and_bad_ptr(void) {
    heap_init();
    heap_stats_t initial = stats();
    void *p = kmalloc(64);
    kfree(p);
    CHECK(panics(do_free, p) == 0, "doble free: no hace panic (se reporta)");
    CHECK(stats().double_free_count == 1, "doble free: queda contado");
    CHECK(same_layout(initial, stats()), "doble free: no corrompe el heap");

    /* doble free de un bloque cuya cabecera ya fue absorbida por una fusion */
    heap_init();
    void *a = kmalloc(64);
    void *b = kmalloc(64);
    void *guard = kmalloc(64);
    (void)guard;
    kfree(b);
    kfree(a);
    CHECK(panics(do_free, b) == 0 && stats().double_free_count == 1,
          "doble free: tambien se detecta sobre una cabecera absorbida");

    heap_init();
    uint8_t *q = kmalloc(64);
    memset(q, 0, 64);
    CHECK(panics(do_free, q + 8) == 1, "kfree de puntero interior (magic invalido) = panic");
    CHECK(panics(do_free, (void *)0x10) == 1, "kfree de puntero fuera del heap = panic");
}

static void test_canary(void) {
    /* 16 B pedidos: el canario va justo despues */
    heap_init();
    uint8_t *p = kmalloc(16);
    CHECK(panics(do_stats, &(heap_stats_t){0}) == 0, "canario: bloque intacto no dispara");
    memset(p + 16, 0, 4);
    CHECK(panics(do_free, p) == 1, "canario: 4 B mas alla del tamano pedido = panic en kfree");

    heap_init();
    p = kmalloc(10);
    memset(p + 10, 0, 4);
    heap_stats_t s;
    CHECK(panics(do_stats, &s) == 1, "canario: tambien lo detecta heap_stats (tamano no multiplo de 4)");
}

static void test_ownership_and_peak(void) {
    heap_init();
    heap_host_tid = 3;
    kmalloc(8);
    kmalloc(8);
    heap_host_tid = 5;
    kmalloc(8);
    heap_host_tid = 0;

    uint32_t bytes = 0;
    uint32_t blocks = 0;
    heap_usage_by_task(3, &bytes, &blocks);
    CHECK(blocks == 2 && bytes == 24, "tarea 3: 2 bloques de (8 B + canario) = 24 B");
    heap_usage_by_task(5, &bytes, &blocks);
    CHECK(blocks == 1, "tarea 5: 1 bloque");
    heap_usage_by_task(9, &bytes, &blocks);
    CHECK(blocks == 0 && bytes == 0, "tarea sin bloques: 0");

    heap_init();
    void *a = kmalloc(4000);
    uint32_t used_high = stats().used;
    kfree(a);
    heap_stats_t s = stats();
    CHECK(s.peak_used >= used_high && s.used < used_high, "peak: la marca de agua sobrevive al free");
    CHECK(s.alloc_count == 1 && s.free_count == 1, "contadores: 1 alloc y 1 free");
}

int main(void) {
    test_init();
    test_alloc_basic();
    test_exhaustion();
    test_free_and_coalesce();
    test_thousand();
    test_fragmentation();
    test_double_free_and_bad_ptr();
    test_canary();
    test_ownership_and_peak();

    printf("\n%d asserts, %d fallos -> %s\n", g_total, g_failed, g_failed ? "FALLO" : "OK");
    return g_failed ? 1 : 0;
}
