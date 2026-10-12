#include <sentinel/kprintf.h>
#include <stdint.h>
#include "../drivers/uart.h"
#include "time.h"
#include <sentinel/task.h>
#include "sched.h"
#include <sentinel/heap.h>
#include "../arch/xtensa/cpu.h"

/* 1 = correr el test de desbordamiento de pila (termina en PANIC a proposito). */
#define RUN_CANARY_TEST 0

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

static void check(const char *name, int ok) {
    kprintf("[%s] %s\r\n", ok ? "PASS" : "FAIL", name);
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
    check("coalescencia: todo liberado = 1 bloque y mismo libre",
          s1.block_count == 1u && s1.free == s0.free);

    uint32_t t0 = cpu_cycles();
    heap_stats(&s1);
    uint32_t us = cycles_to_us(cpu_cycles() - t0);
    kprintf("heap_stats con %u bloque(s): %u us (meta < 100)\r\n", s1.block_count, us);
    check("heap_stats < 100 us", us < 100u);
    kprintf("fin de pruebas E03\r\n");
}

/* Prioridad 0 y duerme: observa a las demas sin estorbarlas. */
static void controller_task(void *arg) {
    (void)arg;
    test_hu07();
    test_hu08();
    test_hu09();
    test_hu10();
    test_e03();
    if (RUN_CANARY_TEST) {
        test_hu11();
    }

    kprintf("=== TODAS LAS PRUEBAS TERMINARON ===\r\n");
}

void kmain(void) {
    uart_init(115200);
    kprintf("Sentinel OS v0.1 -- build %s\n", __DATE__);

    kprintf("deep(12) = %u (esperado 79)\n", deep(12));

    heap_init();
    timer_init(1000);
    sched_init();
    task_create("ctl", controller_task, (void *)0, 0, 0);
    kprintf("controlador creado; kmain es ahora la tarea idle\r\n");

    for (;;) {
    }
}
