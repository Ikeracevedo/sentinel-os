#include <sentinel/kprintf.h>
#include <stdint.h>
#include "../drivers/uart.h"
#include "time.h"
#include <sentinel/task.h>
#include "sched.h"
#include <sentinel/heap.h>
#include <sentinel/panic.h>
#include "selftest.h"
#include <sentinel/vfs.h>
#include <sentinel/procfs.h>
#include "../arch/xtensa/cpu.h"

/* 1 = correr el test de desbordamiento de pila (termina en PANIC a proposito). */
#define RUN_CANARY_TEST 0

/* 1 = pisar el canario de un bloque del heap (termina en PANIC a proposito). */
#define RUN_HEAP_CANARY_TEST 0

/* 1 = en vez de la bateria de pruebas, correr el soak: 5 tareas concurrentes durante
 * SOAK_SECONDS vigilando integridad (E02: cooperativo/apropiativo, 5 min / 30 min / 1 h). */
/* Velocidad del puerto serie. El monitor del Makefile abre a 115200. */
#define UART_BAUD       57600u

#define RUN_SOAK        0
#define SOAK_SECONDS    300u
#define SOAK_REPORT_S   10u

static volatile uint32_t g_c;
static volatile uint32_t g_stop_c;
static volatile uint32_t g_flag[2];

static __attribute__((noinline)) uint32_t deep(uint32_t n) {
    if (n == 0) {
        sched_yield();
        return 1;
    }
    uint32_t r = deep(n - 1);
    __asm__ __volatile__("" : "+r"(r));
    return r + n;
}

static void counter_task(void *arg) {
    (void)arg;
    while (!g_stop_c) {
        g_c++;
    }
}

static void flag_spinner(void *arg) {
    uint32_t i = (uint32_t)(uintptr_t)arg;
    while (!g_flag[i]) {
    }
}

static void quick_task(void *arg) {
    (void)arg;
}

/* ms = ciclos / 40000, sin division de 64 bits: 2^32 / 40000 ~= 107374. */
static uint32_t cycles_to_ms(uint64_t c) {
    return (uint32_t)(c >> 32) * 107374u + (uint32_t)c / 40000u;
}

static void print_ps(void) {
    kprintf("--- tareas ---\r\n");
    for (uint32_t i = 0; i < MAX_TASKS; i++) {
        tcb_t *t = task_get(i);
        if (t != NULL) {
            kprintf("tid=%u %s prio=%u estado=%u\r\n",
                    i, t->name, (uint32_t)t->priority, (uint32_t)t->state);
            kprintf("      cpu=%ums cambios=%u espera_max=%ums\r\n",
                    cycles_to_ms(t->cpu_cycles), t->switches_in, t->max_ready_wait);
        }
    }
}

static void test_hu07(void) {
    uint32_t a;
    uint32_t b;

    int32_t tid = task_create("cnt", counter_task, (void *)0, 3, 0);
    task_sleep(200);
    a = g_c;
    task_sleep(200);
    b = g_c;
    check("tarea READY/RUNNING avanza", b > a);

    check("suspend devuelve 0", task_suspend((uint32_t)tid) == 0);
    task_sleep(50);
    a = g_c;
    task_sleep(200);
    b = g_c;
    check("SUSPENDED no avanza", a == b);
    check("suspend doble = transicion invalida (-1)", task_suspend((uint32_t)tid) == -1);

    check("resume devuelve 0", task_resume((uint32_t)tid) == 0);
    check("resume doble = transicion invalida (-1)", task_resume((uint32_t)tid) == -1);
    task_sleep(200);
    a = g_c;
    task_sleep(200);
    b = g_c;
    check("tras resume vuelve a avanzar", b > a);

    check("tid inexistente devuelve -1", task_suspend(15) == -1);

    g_stop_c = 1;
    task_sleep(50);
    check("task_exit libera la ranura (task_get == NULL)", task_get((uint32_t)tid) == NULL);

    int ok = 1;
    for (uint32_t i = 0; i < 40; i++) {
        if (task_create("q", quick_task, (void *)0, 1, 0) < 0) {
            ok = 0;
            break;
        }
        task_sleep(5);
    }
    check("40 tareas efimeras reciclan ranuras (MAX_TASKS=16)", ok);
    kprintf("fin de pruebas HU-E02-07\r\n");
}

static void test_hu08(void) {
    task_sleep(2500);
    uint32_t idle_free = cpu_idle_percent();
    kprintf("sin carga: idle = %u%% (esperado > 95)\r\n", idle_free);
    check("idle > 95% sin carga", idle_free > 95u);

    g_stop_c = 0;
    task_create("busy", counter_task, (void *)0, 3, 0);
    task_sleep(2500);
    uint32_t idle_busy = cpu_idle_percent();
    kprintf("con 1 tarea girando: idle = %u%% (esperado < 5)\r\n", idle_busy);
    check("idle < 5% con una tarea girando", idle_busy < 5u);

    g_stop_c = 1;
    task_sleep(2500);
    uint32_t idle_back = cpu_idle_percent();
    kprintf("tras detenerla: idle = %u%% (esperado > 95)\r\n", idle_back);
    check("idle vuelve a > 95%", idle_back > 95u);

    tcb_t *idle_tcb = task_get(0);
    check("idle es el tid 0, prioridad 7", idle_tcb != NULL && idle_tcb->priority == 7u);
    check("suspender al idle esta prohibido (-1)", task_suspend(0) == -1);
    kprintf("fin de pruebas HU-E02-08\r\n");
}

static volatile uint32_t g_wake_order[3];
static volatile uint32_t g_wake_n;

/* Duerme 'arg' ms y anota su valor al despertar: asi queda registrado el orden. */
static void sleeper_task(void *arg) {
    uint32_t ms = (uint32_t)(uintptr_t)arg;
    task_sleep(ms);
    g_wake_order[g_wake_n++] = ms;
}

static void test_hu09(void) {
    uint32_t t0 = cpu_cycles();
    task_sleep(1000);
    uint32_t real_ms = cycles_to_ms(cpu_cycles() - t0);
    kprintf("sleep(1000) midio %u ms (aceptable: 980..1020)\r\n", real_ms);
    check("sleep(1000) con error < 2%", real_ms >= 980u && real_ms <= 1020u);

    g_wake_n = 0;
    task_create("s300", sleeper_task, (void *)(uintptr_t)300, 1, 0);
    task_create("s100", sleeper_task, (void *)(uintptr_t)100, 1, 0);
    task_create("s200", sleeper_task, (void *)(uintptr_t)200, 1, 0);
    task_sleep(500);
    kprintf("orden de despertar: %u %u %u\r\n",
            g_wake_order[0], g_wake_order[1], g_wake_order[2]);
    check("despiertan en orden 100, 200, 300",
          g_wake_n == 3u && g_wake_order[0] == 100u &&
          g_wake_order[1] == 200u && g_wake_order[2] == 300u);

    uint32_t u0 = uptime_ms();
    task_sleep(250);
    uint32_t du = uptime_ms() - u0;
    check("uptime_ms avanza ~250 ms", du >= 245u && du <= 255u);
    kprintf("fin de pruebas HU-E02-09\r\n");
}


/* Cada nivel reserva 64 B y los escribe todos: la pila de 1 KB se pasa de largo
 * y pisa el canario. Al fondo gira para que el tick la saque de la CPU. */
static volatile uint32_t g_ovf_stop;   /* nunca se enciende: solo evita "recursion infinita" */

static __attribute__((noinline)) void overflow_rec(uint32_t n) {
    volatile uint8_t buf[64];
    for (uint32_t i = 0; i < sizeof(buf); i++) {
        buf[i] = 0xAA;
    }
    if (n > 0) {
        overflow_rec(n - 1);
    }
    while (!g_ovf_stop) {
    }
}

static void overflow_task(void *arg) {
    (void)arg;
    overflow_rec(14);
}

static void test_hu11(void) {
    kprintf("provocando desbordamiento: se espera PANIC con 'ovf'\r\n");
    task_create("ovf", overflow_task, (void *)0, 1, 1024);
    task_sleep(500);
    kprintf("[FAIL] el canario no detecto el desbordamiento\r\n");
}


static void test_hu10(void) {
    g_flag[0] = 0;
    g_flag[1] = 0;
    task_create("H", flag_spinner, (void *)0, 1, 0);
    int32_t l = task_create("L", flag_spinner, (void *)1, 5, 0);

    task_sleep(1500);
    g_flag[0] = 1;
    task_sleep(100);

    tcb_t *lt = task_get((uint32_t)l);
    check("L llego a ejecutar (switches_in > 0)", lt != NULL && lt->switches_in > 0);
    check("L: espera maxima >= 1000 ms (inanicion medida)",
          lt != NULL && lt->max_ready_wait >= 1000u);
    print_ps();

    g_flag[1] = 1;
    task_sleep(50);

    uint32_t cost = sched_stats_avg_cycles();
    kprintf("costo medio de las estadisticas por cambio: %u ciclos (meta < 20)\r\n", cost);
    check("estadisticas < 20 ciclos por cambio", cost < 20u);
    kprintf("fin de pruebas HU-E02-10\r\n");
}

#define FILL_MAX 800u
static void *g_ptrs[FILL_MAX];

/* Llena el heap, lo deja como queso suizo y mide: fragmentacion (HU-E03-04) y
 * cuanto tiempo mantiene apagadas las interrupciones un recorrido (meta 100 us). */
static void test_e03_fill(void) {
    heap_stats_t base;
    heap_stats_t s;
    heap_stats(&base);

    uint32_t n = 0;
    while (n < FILL_MAX && (g_ptrs[n] = kmalloc(100)) != NULL) {
        n++;
    }
    kprintf("heap lleno con %u bloques de 100 B\r\n", n);
    check("el heap se llena y kmalloc devuelve NULL sin corromperse",
          n > 700u && n < FILL_MAX && kmalloc(100) == NULL);

    for (uint32_t i = 0; i < n; i += 2) {
        kfree(g_ptrs[i]);
    }

    /* Con las interrupciones enmascaradas el tick no se cuela en la medicion: se
     * mide el costo puro del recorrido, y de ahi lo que dura una tanda. */
    uint32_t ps = irq_disable();
    uint32_t t0 = cpu_cycles();
    heap_stats(&s);
    uint32_t cyc = cpu_cycles() - t0;
    irq_restore(ps);
    uint32_t per_block = cyc / s.block_count;
    uint32_t tanda_us = cycles_to_us(per_block * HEAP_WALK_CHUNK);
    uint32_t frag = heap_fragmentation();
    kprintf("%u bloques: libre=%u mayor=%u frag=%u%%\r\n",
            s.block_count, s.free, s.largest_free_block, frag);
    kprintf("recorrido: %u ciclos/bloque; tanda de %u bloques ~%u us (meta < 100)\r\n",
            per_block, HEAP_WALK_CHUNK, tanda_us);

    check("fragmentacion > 40% con la mitad de los bloques libres", frag > 40u);
    check("hay memoria libre pero kmalloc(mitad de lo libre) falla",
          kmalloc(s.free / 2u) == NULL);
    check("tanda de recorrido < 100 us con interrupciones apagadas", tanda_us < 100u);
    check("nadie toco la lista: 0 reinicios del recorrido", s.walk_restarts == 0u);

    for (uint32_t i = 1; i < n; i += 2) {
        kfree(g_ptrs[i]);
    }
    heap_stats(&s);
    check("tras liberar todo: estado inicial exacto (1 bloque, mismo libre)",
          s.block_count == base.block_count && s.free == base.free &&
          heap_fragmentation() == 0u);
}

static volatile uint32_t g_leak_stop;

/* Reserva 5 bloques y se duerme sin liberarlos: una fuga controlada. */
static void leaker_task(void *arg) {
    (void)arg;
    void *held[5];
    for (uint32_t i = 0; i < 5; i++) {
        held[i] = kmalloc(200);
    }
    while (!g_leak_stop) {
        task_sleep(10);
    }
    for (uint32_t i = 0; i < 5; i++) {
        kfree(held[i]);
    }
}

/* HU-E03-06: "hay una fuga" no sirve; "leaker tiene 5 bloques" si. */
static void test_e03_leak(void) {
    g_leak_stop = 0;
    int32_t tid = task_create("leaker", leaker_task, (void *)0, 1, 0);
    task_sleep(100);

    uint32_t bytes = 0;
    uint32_t blocks = 0;
    heap_usage_by_task((uint8_t)tid, &bytes, &blocks);
    tcb_t *t = task_get((uint32_t)tid);
    kprintf("la tarea '%s' tiene %u bloques (%u bytes) sin liberar\r\n",
            (t != NULL) ? t->name : "?", blocks, bytes);
    check("una fuga se atribuye a su tarea por nombre", t != NULL && blocks == 5u);

    g_leak_stop = 1;
    task_sleep(100);
    heap_usage_by_task((uint8_t)tid, &bytes, &blocks);
    check("al liberar, la tarea queda con 0 bloques", blocks == 0u);
}

/* HU-E03-05: cuanto cuesta el canario. Se mide el par alloc+free completo y, aparte,
 * solo las operaciones del canario (escribir, leer, comparar) menos un bucle vacio. */
static void test_e03_cost(void) {
    enum { N = 1000 };

    uint32_t t0 = cpu_cycles();
    for (uint32_t i = 0; i < N; i++) {
        void *p = kmalloc(64);
        kfree(p);
    }
    uint32_t pair = (cpu_cycles() - t0) / N;

    volatile uint32_t slot = 0;
    uint32_t bad = 0;
    t0 = cpu_cycles();
    for (uint32_t i = 0; i < N; i++) {
        slot = 0xC4A4A210u;
        if (slot != 0xC4A4A210u) {
            bad++;
        }
    }
    uint32_t with_canary = cpu_cycles() - t0;

    t0 = cpu_cycles();
    for (uint32_t i = 0; i < N; i++) {
        __asm__ __volatile__("" ::: "memory");
    }
    uint32_t empty = cpu_cycles() - t0;

    uint32_t canary = (with_canary > empty) ? (with_canary - empty) / N : 0u;
    kprintf("kmalloc+kfree = %u ciclos; canario (escribir+leer+comparar) ~%u ciclos (%u)\r\n",
            pair, canary, bad);
    check("kmalloc+kfree < 4000 ciclos (100 us)", pair < 4000u);
    check("el canario cuesta < 10% del par alloc+free", canary * 10u < pair);
}

/* HU-E03-05: pisar 4 bytes pasado el bloque. Termina en PANIC con bloque, tamano y tarea. */
static void test_e03_corrupt(void) {
    uint8_t *p = kmalloc(16);
    kprintf("escribiendo 4 bytes mas alla del bloque: se espera PANIC en kfree\r\n");
    p[16] = 0;
    p[17] = 0;
    p[18] = 0;
    p[19] = 0;
    kfree(p);
    kprintf("[FAIL] el canario del heap no detecto la escritura\r\n");
}

/* E03: allocator en la placa. La logica fina ya se probo en el host (make test);
 * aqui se comprueba que corre en Xtensa: alineacion, split/coalescencia, tiempo. */
static void test_e03(void) {
    heap_stats_t s0;
    heap_stats_t s1;
    heap_stats(&s0);
    check("heap recien iniciado: frag 0%", heap_fragmentation() == 0);

    void *p[8];
    int ok = 1;
    for (uint32_t i = 0; i < 8; i++) {
        p[i] = kmalloc(100 + i * 50);
        if (p[i] == NULL || ((uintptr_t)p[i] & 3u) != 0) {
            ok = 0;
        }
    }
    check("8 allocs no nulos y alineados a 4 B", ok);

    uint32_t bytes = 0;
    uint32_t blocks = 0;
    heap_usage_by_task(sched_current()->tid, &bytes, &blocks);
    kprintf("tarea '%s': %u bloques, %u bytes\r\n", sched_current()->name, blocks, bytes);
    check("contabilidad por tarea: 8 bloques de 'ctl'", blocks == 8u);

    for (uint32_t i = 1; i < 8; i += 2) {
        kfree(p[i]);
    }
    uint32_t frag = heap_fragmentation();
    kprintf("fragmentacion tras liberar bloques alternos: %u%%\r\n", frag);
    check("la fragmentacion sube", frag > 0);
    heap_dump();

    for (uint32_t i = 0; i < 8; i += 2) {
        kfree(p[i]);
    }
    heap_stats(&s1);
    check("coalescencia: todo liberado = mismos bloques y mismo libre que al empezar",
          s1.block_count == s0.block_count && s1.free == s0.free);

    uint32_t t0 = cpu_cycles();
    heap_stats(&s1);
    uint32_t us = cycles_to_us(cpu_cycles() - t0);
    kprintf("heap_stats con %u bloque(s): %u us (meta < 100)\r\n", s1.block_count, us);
    check("heap_stats < 100 us", us < 100u);

    test_e03_fill();
    test_e03_leak();
    test_e03_cost();
    kprintf("fin de pruebas E03\r\n");
}

/* ---- SOAK ---------------------------------------------------------------
 * Cinco tareas corren juntas mucho tiempo. En vez de solo "no se cuelga", cada una
 * comprueba una propiedad que un cambio de contexto defectuoso rompe en silencio. */

static volatile uint32_t g_soak_errors;
static volatile uint32_t g_soak_progress[5];  /* 0-2 integridad, 3 heap, 4 durmiente */
static volatile uint32_t g_soak_spawned;
static volatile uint32_t g_soak_stop;

/* Calculo con desplazamientos variables (registro SAR) dentro de un bucle contado
 * (LCOUNT/LBEG/LEND): exactamente el estado extra que el marco de contexto guarda. */
static __attribute__((noinline)) uint32_t soak_mix(uint32_t seed) {
    uint32_t x = seed;
    for (uint32_t i = 0; i < 2000u; i++) {
        x = x * 1664525u + 1013904223u;
        x ^= x >> (i & 15u);
        x += (x << ((i >> 2) & 7u)) ^ i;
    }
    return x;
}

/* Mismo calculo, pero cediendo la CPU a mitad de camino con sched_yield() (camino
 * por syscall, el "cooperativo"): x, i y el resto de registros deben sobrevivir. */
static __attribute__((noinline)) uint32_t soak_mix_yield(uint32_t seed) {
    uint32_t x = seed;
    for (uint32_t i = 0; i < 2000u; i++) {
        x = x * 1664525u + 1013904223u;
        x ^= x >> (i & 15u);
        x += (x << ((i >> 2) & 7u)) ^ i;
        if ((i & 127u) == 0u) {
            sched_yield();
        }
    }
    return x;
}

/* El resultado correcto se calcula una vez SIN interrupciones; despues se repite
 * sin parar, y el tick la interrumpe en puntos arbitrarios. Si un registro no
 * sobrevive al cambio de contexto, el resultado difiere y se cuenta un error.
 * La tarea 2 usa la variante que tambien cede con sched_yield(). */
static void soak_integrity_task(void *arg) {
    uint32_t id = (uint32_t)(uintptr_t)arg;
    uint32_t seed = 0x1000u + id * 7919u;
    uint32_t (*mix)(uint32_t) = (id == 2u) ? soak_mix_yield : soak_mix;

    uint32_t ps = irq_disable();
    uint32_t expected = soak_mix(seed);   /* sin cesiones: el resultado es identico */
    irq_restore(ps);

    while (!g_soak_stop) {
        if (mix(seed) != expected) {
            g_soak_errors++;
        }
        g_soak_progress[id]++;
    }
}

/* Reserva y libera bloques al azar. Cada bloque se llena con un patron que depende
 * de su direccion y se verifica antes de liberarlo: si dos bloques se solapan o el
 * heap se corrompe, el patron no coincide. */
static void soak_heap_task(void *arg) {
    (void)arg;
    enum { SLOTS = 24 };
    uint32_t *hold[SLOTS];
    uint32_t words[SLOTS];
    for (uint32_t i = 0; i < SLOTS; i++) {
        hold[i] = NULL;
    }
    uint32_t rng = 12345u;

    while (!g_soak_stop) {
        rng = rng * 1664525u + 1013904223u;
        uint32_t slot = (rng >> 16) % SLOTS;
        if (hold[slot] != NULL) {
            uint32_t key = (uint32_t)(uintptr_t)hold[slot];
            for (uint32_t j = 0; j < words[slot]; j++) {
                if (hold[slot][j] != (key ^ j)) {
                    g_soak_errors++;
                    break;
                }
            }
            kfree(hold[slot]);
            hold[slot] = NULL;
        } else {
            uint32_t n = 4u + ((rng >> 8) % 100u);     /* 4..103 palabras */
            uint32_t *w = kmalloc(n * 4u);
            if (w != NULL) {
                uint32_t key = (uint32_t)(uintptr_t)w;
                for (uint32_t j = 0; j < n; j++) {
                    w[j] = key ^ j;
                }
                hold[slot] = w;
                words[slot] = n;
            }
        }
        g_soak_progress[3]++;
    }
    for (uint32_t i = 0; i < SLOTS; i++) {
        kfree(hold[i]);
    }
}

static void soak_quick_task(void *arg) {
    (void)arg;
    g_soak_spawned++;
}

/* Duerme, despierta y crea una tarea efimera de mayor prioridad: ejercita sleep,
 * apropiacion por despertar, task_exit y el reciclaje de ranuras bajo carga. */
static void soak_sleeper_task(void *arg) {
    (void)arg;
    while (!g_soak_stop) {
        task_sleep(100);
        if (task_create("q", soak_quick_task, (void *)0, 1, 0) < 0) {
            g_soak_errors++;
        }
        g_soak_progress[4]++;
    }
}

static void soak_run(void) {
    heap_stats_t base;
    heap_stats_t hs;
    heap_stats(&base);
    kprintf("SOAK: %u s, 5 tareas (3 de integridad, 1 de heap, 1 durmiente)\r\n", SOAK_SECONDS);

    int32_t tid[5];
    tid[0] = task_create("integA", soak_integrity_task, (void *)0, 3, 0);
    tid[1] = task_create("integB", soak_integrity_task, (void *)1, 3, 0);
    tid[2] = task_create("integC", soak_integrity_task, (void *)2, 3, 0);
    tid[3] = task_create("heapt",  soak_heap_task,      (void *)0, 3, 0);
    tid[4] = task_create("sleepr", soak_sleeper_task,   (void *)0, 2, 0);
    for (uint32_t i = 0; i < 5; i++) {
        if (tid[i] < 0) {
            panic("soak: no se pudo crear una tarea");
        }
    }

    uint32_t prev[5] = {0, 0, 0, 0, 0};
    uint32_t stalled = 0;
    for (uint32_t elapsed = 0; elapsed < SOAK_SECONDS; elapsed += SOAK_REPORT_S) {
        task_sleep(SOAK_REPORT_S * 1000u);

        for (uint32_t i = 0; i < 5; i++) {
            if (g_soak_progress[i] == prev[i]) {
                stalled++;
                kprintf("SOAK: la tarea %u no avanzo en %u s\r\n", i, SOAK_REPORT_S);
            }
            prev[i] = g_soak_progress[i];
        }
        heap_stats(&hs);
        kprintf("[%us] errores=%u idle=%u%% heap: bloques=%u frag=%u%% reinicios=%u creadas=%u\r\n",
                elapsed + SOAK_REPORT_S, g_soak_errors, cpu_idle_percent(),
                hs.block_count, heap_fragmentation(), hs.walk_restarts, g_soak_spawned);

        uint32_t ms[4];
        for (uint32_t i = 0; i < 4; i++) {
            tcb_t *t = task_get((uint32_t)tid[i]);
            ms[i] = (t != NULL) ? cycles_to_ms(t->cpu_cycles) : 0u;
        }
        kprintf("      cpu: integA=%ums integB=%ums integC=%ums heapt=%ums\r\n",
                ms[0], ms[1], ms[2], ms[3]);
    }

    g_soak_stop = 1;
    task_sleep(1000);
    heap_stats(&hs);
    int leak_free = (hs.block_count == base.block_count && hs.free == base.free);
    int ok = (g_soak_errors == 0u && stalled == 0u && leak_free);
    kprintf("SOAK %s: %u s, errores=%u, tareas_sin_avance=%u, heap_restaurado=%u\r\n",
            ok ? "PASS" : "FAIL", SOAK_SECONDS, g_soak_errors, stalled, (uint32_t)leak_free);
    kprintf("TXBYTES: %u\r\n", uart_tx_count());
}

/* Prioridad 0 y duerme: observa a las demas sin estorbarlas. */
static void controller_task(void *arg) {
    (void)arg;
    if (RUN_SOAK) {
        soak_run();
        return;
    }
    test_hu07();
    test_hu08();
    test_hu09();
    test_hu10();
    test_e03();
    test_e04();
    test_e05();
    if (RUN_HEAP_CANARY_TEST) {
        test_e03_corrupt();
    }
    if (RUN_CANARY_TEST) {
        test_hu11();
    }

    kprintf("TXBYTES: %u\r\n", uart_tx_count());
    selftest_summary();
    kprintf("=== TODAS LAS PRUEBAS TERMINARON ===\r\n");
}

void kmain(void) {
    uart_init(UART_BAUD);
    kprintf("Sentinel OS v0.1 -- build %s\n", __DATE__);

    kprintf("deep(12) = %u (esperado 79)\n", deep(12));

    uint32_t arena_bad = heap_probe_region();
    kprintf("arena del heap en %p: %u palabras con error (esperado 0)\r\n",
            (void *)(uintptr_t)heap_arena_address(), arena_bad);
    if (arena_bad != 0u) {
        panic("la region del heap no es RAM fiable");
    }
    heap_init();
    vfs_init();
    procfs_init();
    vfs_populate_demo();
    timer_init(1000);
    sched_init();
    task_create("ctl", controller_task, (void *)0, 0, 0);
    kprintf("controlador creado; kmain es ahora la tarea idle\r\n");

    for (;;) {
    }
}
