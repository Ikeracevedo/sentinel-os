/* Pruebas en placa de E04 (sincronizacion). Las ejecuta la tarea 'ctl' (prioridad 0). */

#include "selftest.h"
#include <sentinel/sync.h>
#include <sentinel/task.h>
#include <sentinel/heap.h>
#include <sentinel/kprintf.h>
#include "sched.h"
#include "time.h"
#include "../arch/xtensa/cpu.h"

static uint32_t read_ps(void) {
    uint32_t ps;
    __asm__ __volatile__("rsr %0, ps" : "=r"(ps));
    return ps;
}

/* ---- HU-E04-01: secciones criticas ---- */

static void t_critical(void) {
    uint32_t base = read_ps() & 0xFu;
    uint32_t a = irq_save();
    uint32_t b = irq_save();
    irq_restore(b);
    uint32_t mid = read_ps() & 0xFu;
    irq_restore(a);
    uint32_t end = read_ps() & 0xFu;
    check("irq_save anidable: tras restaurar la interior sigue apagado (nivel 15)", mid == 15u);
    check("irq_restore deja el estado original exacto", end == base);

    uint32_t inside;
    uint32_t outside;
    {
        CRITICAL_ENTER();
        inside = read_ps() & 0xFu;
        CRITICAL_EXIT();
    }
    outside = read_ps() & 0xFu;
    check("CRITICAL_ENTER apaga y CRITICAL_EXIT restaura", inside == 15u && outside == base);
}

/* ---- HU-E04-02: la carrera y el mutex ---- */

static volatile uint32_t g_shared;
static volatile uint32_t g_done[3];
static mutex_t g_mx;

#define RACE_ITERS 60000u
#define RACE_TASKS 3u
#define LOCK_ROUNDS 100u
#define LOCK_CHUNK 600u            /* LOCK_ROUNDS * LOCK_CHUNK == RACE_ITERS */

/* Incremento NO atomico con "trabajo" entre leer y escribir. Con un simple g_shared++
 * la ventana son 2 instrucciones y, como el tick es periodico y el sistema determinista,
 * la interrupcion cae siempre fuera de ella (el test no demostraba nada). Un calculo
 * real entre la lectura y la escritura es lo que pasa en codigo de verdad. */
static void racy_increment(void) {
    uint32_t v = g_shared;
    for (volatile uint32_t k = 0; k < 8u; k++) {
    }
    g_shared = v + 1u;
}

static void racer_nolock(void *arg) {
    uint32_t id = (uint32_t)(uintptr_t)arg;
    for (uint32_t i = 0; i < RACE_ITERS; i++) {
        racy_increment();
    }
    g_done[id] = 1;
}

static void racer_lock(void *arg) {
    uint32_t id = (uint32_t)(uintptr_t)arg;
    for (uint32_t r = 0; r < LOCK_ROUNDS; r++) {
        mutex_lock(&g_mx);
        for (uint32_t i = 0; i < LOCK_CHUNK; i++) {
            racy_increment();
        }
        mutex_unlock(&g_mx);
    }
    g_done[id] = 1;
}

static void wait_all_done(void) {
    while (!(g_done[0] && g_done[1] && g_done[2])) {
        task_sleep(20);
    }
}

static void t_race_and_mutex(void) {
    const uint32_t expected = RACE_TASKS * RACE_ITERS;

    g_shared = 0;
    for (uint32_t i = 0; i < RACE_TASKS; i++) {
        g_done[i] = 0;
        task_create("race", racer_nolock, (void *)(uintptr_t)i, 3, 0);
    }
    wait_all_done();
    uint32_t raced = g_shared;
    kprintf("sin mutex: %u de %u (se perdieron %u incrementos)\r\n", raced, expected, expected - raced);
    check("sin mutex la carrera pierde incrementos (resultado incorrecto)", raced != expected);

    g_shared = 0;
    for (uint32_t i = 0; i < RACE_TASKS; i++) {
        g_done[i] = 0;
        task_create("lock", racer_lock, (void *)(uintptr_t)i, 3, 0);
    }
    wait_all_done();
    uint32_t locked = g_shared;
    kprintf("con mutex: %u de %u\r\n", locked, RACE_TASKS * LOCK_ROUNDS * LOCK_CHUNK);
    check("con mutex el contador compartido es EXACTO", locked == RACE_TASKS * LOCK_ROUNDS * LOCK_CHUNK);
    check("el mutex cuenta sus adquisiciones (3 x 100 = 300)", g_mx.acquisitions == RACE_TASKS * LOCK_ROUNDS);
}

/* ---- propietario y reentrada ---- */

static mutex_t g_own;

static void t_ownership(void) {
    mutex_init(&g_own, "own");
    check("unlock de un mutex libre (sin ser dueno) devuelve -1", mutex_unlock(&g_own) == -1);
    check("mutex_lock libre devuelve 0 y ctl pasa a ser el dueno",
          mutex_lock(&g_own) == 0 && g_own.owner == sched_current());
    check("relock del propio dueno devuelve -2 (no recursivo)", mutex_lock(&g_own) == -2);
    check("unlock del dueno devuelve 0 y lo deja libre",
          mutex_unlock(&g_own) == 0 && g_own.owner == NULL);
    mutex_destroy(&g_own);
}

/* ---- cola FIFO y grafo de espera ---- */

static mutex_t g_fifo;
static volatile uint32_t g_order[3];
static volatile uint32_t g_order_n;

static void fifo_waiter(void *arg) {
    uint32_t id = (uint32_t)(uintptr_t)arg;
    mutex_lock(&g_fifo);
    g_order[g_order_n++] = id;
    mutex_unlock(&g_fifo);
}

static void t_fifo(void) {
    mutex_init(&g_fifo, "fifo");
    tcb_t *me = sched_current();
    mutex_lock(&g_fifo);

    g_order_n = 0;
    int32_t tid[3];
    for (uint32_t i = 0; i < 3; i++) {
        tid[i] = task_create("w", fifo_waiter, (void *)(uintptr_t)(i + 1u), 2, 0);
        task_sleep(20);   /* cada una llega a la cola antes de crear la siguiente */
    }
    int blocked = 1;
    int graph = 1;
    for (uint32_t i = 0; i < 3; i++) {
        tcb_t *w = task_get((uint32_t)tid[i]);
        if (w == NULL || w->state != TASK_BLOCKED) {
            blocked = 0;
        } else if (w->waiting_on != &g_fifo || g_fifo.owner != me) {
            graph = 0;   /* arista w -> fifo -> ctl */
        }
    }
    check("3 tareas esperan en estado BLOCKED con 3 en la cola", blocked && g_fifo.q.count == 3u);
    check("grafo: cada esperador apunta (waiting_on) al mutex cuyo dueno es ctl", graph);

    mutex_unlock(&g_fifo);        /* traspaso al primero de la cola */
    task_sleep(100);
    check("se despiertan en orden FIFO (1, 2, 3)",
          g_order_n == 3u && g_order[0] == 1u && g_order[1] == 2u && g_order[2] == 3u);
    check("al terminar nadie espera y el mutex queda libre",
          g_fifo.q.count == 0u && g_fifo.owner == NULL);
    mutex_destroy(&g_fifo);
}

/* ---- HU-E04-05: timeouts ---- */

static mutex_t g_tm;
static volatile uint32_t g_hold_release;

static void holder_task(void *arg) {
    (void)arg;
    mutex_lock(&g_tm);
    while (!g_hold_release) {
        task_sleep(10);
    }
    mutex_unlock(&g_tm);
}

static void t_timeouts(void) {
    mutex_init(&g_tm, "tm");
    g_hold_release = 0;
    task_create("holder", holder_task, (void *)0, 2, 0);
    task_sleep(50);               /* holder toma el mutex */

    tcb_t *me = sched_current();
    check("try-lock (plazo 0) sobre un mutex ocupado devuelve -1 sin bloquear",
          mutex_lock_timeout(&g_tm, 0) == -1);

    uint32_t t0 = uptime_ms();
    int r = mutex_lock_timeout(&g_tm, 200);
    uint32_t dt = uptime_ms() - t0;
    kprintf("mutex_lock_timeout(200) -> %d tras %u ms\r\n", r, dt);
    check("mutex_lock_timeout devuelve -1 al expirar", r == -1);
    check("el plazo se respeta (195..230 ms)", dt >= 195u && dt <= 230u);
    check("al expirar: waiting_on limpio, estado RUNNING y fuera de la cola",
          me->waiting_on == NULL && me->waiting_kind == 0u && g_tm.q.count == 0u &&
          me->state == TASK_RUNNING);

    g_hold_release = 1;
    task_sleep(50);
    check("tras liberarse, el mutex se adquiere normalmente (sin esperador fantasma)",
          mutex_lock_timeout(&g_tm, 100) == 0 && g_tm.owner == me);
    mutex_unlock(&g_tm);

    sem_t s;
    sem_init(&s, 0);
    t0 = uptime_ms();
    r = sem_wait_timeout(&s, 100);
    dt = uptime_ms() - t0;
    check("sem_wait_timeout expira (-1) tras ~100 ms", r == -1 && dt >= 95u && dt <= 130u);
    mutex_destroy(&g_tm);
}

/* ---- HU-E04-03: productor / consumidor con buffer de 5 ---- */

#define PC_ITEMS 200u
#define PC_SLOTS 5u

static sem_t g_pc_empty;
static sem_t g_pc_full;
static uint32_t g_pc_buf[PC_SLOTS];
static uint32_t g_pc_in;
static uint32_t g_pc_out;
static volatile uint32_t g_pc_consumed;
static volatile uint32_t g_pc_errors;
static volatile uint32_t g_pc_done;

static void producer_task(void *arg) {
    (void)arg;
    for (uint32_t v = 1; v <= PC_ITEMS; v++) {
        sem_wait(&g_pc_empty);
        g_pc_buf[g_pc_in] = v;
        g_pc_in = (g_pc_in + 1u) % PC_SLOTS;
        sem_post(&g_pc_full);
    }
}

static void consumer_task(void *arg) {
    (void)arg;
    uint32_t expect = 1;
    for (uint32_t n = 0; n < PC_ITEMS; n++) {
        sem_wait(&g_pc_full);
        uint32_t v = g_pc_buf[g_pc_out];
        g_pc_out = (g_pc_out + 1u) % PC_SLOTS;
        sem_post(&g_pc_empty);
        if (v != expect) {
            g_pc_errors++;       /* hueco o duplicado */
        }
        expect++;
        g_pc_consumed++;
        if ((n % 10u) == 0u) {
            task_sleep(2);       /* consumidor mas lento: el productor se bloquea con el buffer lleno */
        }
    }
    g_pc_done = 1;
}

static void t_producer_consumer(void) {
    sem_init(&g_pc_empty, (int32_t)PC_SLOTS);
    sem_init(&g_pc_full, 0);
    g_pc_in = g_pc_out = 0;
    g_pc_consumed = g_pc_errors = g_pc_done = 0;
    task_create("prod", producer_task, (void *)0, 3, 0);
    task_create("cons", consumer_task, (void *)0, 3, 0);
    for (uint32_t i = 0; i < 100 && !g_pc_done; i++) {
        task_sleep(20);
    }
    check("productor/consumidor (buffer de 5): 200 elementos sin perdidas ni duplicados",
          g_pc_done && g_pc_consumed == PC_ITEMS && g_pc_errors == 0u);
    check("al final los semaforos vuelven al estado inicial (5 libres, 0 llenos)",
          g_pc_empty.count == (int32_t)PC_SLOTS && g_pc_full.count == 0);
}

/* ---- HU-E04-03: sem_post desde un ISR ---- */

static sem_t g_isr_sem;
static volatile uint32_t g_hook_ticks;
static volatile uint32_t g_isr_got;
static volatile uint32_t g_isr_t0;
static volatile uint32_t g_isr_t1;

static void tick_hook_test(void) {
    if ((++g_hook_ticks % 100u) == 0u) {
        sem_post(&g_isr_sem);        /* en contexto de interrupcion */
    }
}

static void isr_waiter(void *arg) {
    (void)arg;
    g_isr_t0 = uptime_ms();
    for (uint32_t i = 0; i < 10; i++) {
        sem_wait(&g_isr_sem);
        g_isr_got++;
    }
    g_isr_t1 = uptime_ms();
}

static void t_sem_from_isr(void) {
    sem_init(&g_isr_sem, 0);
    g_hook_ticks = g_isr_got = 0;
    sched_add_tick_hook(tick_hook_test);
    task_create("isrw", isr_waiter, (void *)0, 1, 0);
    task_sleep(1300);
    sched_remove_tick_hook(tick_hook_test);
    uint32_t span = g_isr_t1 - g_isr_t0;
    kprintf("sem_post desde ISR: %u despertares en %u ms\r\n", g_isr_got, span);
    check("sem_post desde el tick despierta a la tarea 10 veces, una cada ~100 ms",
          g_isr_got == 10u && span >= 950u && span <= 1050u);
}

/* ---- HU-E04-04: grafo de espera con un deadlock provocado ---- */

static mutex_t g_dA;
static mutex_t g_dB;
static volatile int g_dres[2];

static void dead1(void *arg) {
    (void)arg;
    mutex_lock(&g_dA);
    task_sleep(100);
    int r = mutex_lock_timeout(&g_dB, 800);
    g_dres[0] = r;
    if (r == 0) {
        mutex_unlock(&g_dB);
    }
    mutex_unlock(&g_dA);
}

static void dead2(void *arg) {
    (void)arg;
    mutex_lock(&g_dB);
    task_sleep(100);
    int r = mutex_lock_timeout(&g_dA, 800);
    g_dres[1] = r;
    if (r == 0) {
        mutex_unlock(&g_dA);
    }
    mutex_unlock(&g_dB);
}

static void t_deadlock(void) {
    mutex_init(&g_dA, "A");
    mutex_init(&g_dB, "B");
    g_dres[0] = g_dres[1] = 99;
    int32_t t1 = task_create("dead1", dead1, (void *)0, 2, 0);
    int32_t t2 = task_create("dead2", dead2, (void *)0, 2, 0);
    task_sleep(400);              /* ya se bloquearon cruzados */

    tcb_t *d1 = task_get((uint32_t)t1);
    tcb_t *d2 = task_get((uint32_t)t2);
    int cycle = d1 != NULL && d2 != NULL &&
                d1->waiting_on == &g_dB && g_dB.owner == d2 &&
                d2->waiting_on == &g_dA && g_dA.owner == d1;
    check("deadlock A->B->A reflejado en el grafo (waiting_on / owner)", cycle);
    check("ambas tareas figuran BLOCKED", d1 != NULL && d2 != NULL &&
          d1->state == TASK_BLOCKED && d2->state == TASK_BLOCKED);
    sync_dump();

    task_sleep(1000);             /* los timeouts rompen el deadlock */
    check("los timeouts sacan a las tareas del deadlock (resultado -1)",
          g_dres[0] == -1 || g_dres[1] == -1);
    check("tras el deadlock ambos mutex quedan libres y sin esperadores",
          g_dA.owner == NULL && g_dB.owner == NULL && g_dA.q.count == 0u && g_dB.q.count == 0u);
    check("y las tareas ya terminaron", task_get((uint32_t)t1) == NULL && task_get((uint32_t)t2) == NULL);
    mutex_destroy(&g_dA);
    mutex_destroy(&g_dB);
}

/* ---- kill: libera cola, mutex y heap ---- */

static mutex_t g_km;
static volatile uint32_t g_kflag;

static void kill_blocked(void *arg) {
    (void)arg;
    mutex_lock(&g_km);
    g_kflag = 99;
}

static void kill_owner(void *arg) {
    (void)arg;
    mutex_lock(&g_km);
    for (;;) {
        task_sleep(100);
    }
}

static void kill_waiter(void *arg) {
    (void)arg;
    mutex_lock(&g_km);
    g_kflag = 1;
    mutex_unlock(&g_km);
}

static void kill_alloc(void *arg) {
    (void)arg;
    kmalloc(40);
    kmalloc(40);
    kmalloc(40);
    for (;;) {
        task_sleep(100);
    }
}

static void t_kill(void) {
    mutex_init(&g_km, "km");
    tcb_t *me = sched_current();

    mutex_lock(&g_km);
    int32_t kb = task_create("kb", kill_blocked, (void *)0, 2, 0);
    task_sleep(50);
    tcb_t *kbt = task_get((uint32_t)kb);
    check("la tarea esperando esta BLOCKED y en la cola", kbt != NULL && kbt->state == TASK_BLOCKED && g_km.q.count == 1u);
    check("task_kill de una tarea BLOCKED devuelve 0", task_kill((uint32_t)kb) == 0);
    check("kill: sale de la cola de espera y desaparece", g_km.q.count == 0u && task_get((uint32_t)kb) == NULL);
    mutex_unlock(&g_km);
    check("al soltar no hay traspaso a un esperador fantasma", g_km.owner == NULL && g_kflag != 99u);

    g_kflag = 0;
    int32_t ko = task_create("ko", kill_owner, (void *)0, 2, 0);
    task_sleep(50);
    int32_t kw = task_create("kw", kill_waiter, (void *)0, 2, 0);
    task_sleep(50);
    tcb_t *kot = task_get((uint32_t)ko);
    tcb_t *kwt = task_get((uint32_t)kw);
    check("ko es el dueno y kw espera", kot != NULL && kwt != NULL && g_km.owner == kot && kwt->state == TASK_BLOCKED);
    task_kill((uint32_t)ko);
    check("matar al dueno traspasa el mutex al primer esperador", g_km.owner == kwt);
    task_sleep(50);
    check("kw ejecuto con el mutex traspasado y lo solto", g_kflag == 1u && g_km.owner == NULL);

    int32_t ka = task_create("ka", kill_alloc, (void *)0, 2, 0);
    task_sleep(50);
    uint32_t bytes = 0;
    uint32_t before = 0;
    uint32_t after = 0;
    heap_usage_by_task((uint8_t)ka, &bytes, &before);
    task_kill((uint32_t)ka);
    heap_usage_by_task((uint8_t)ka, &bytes, &after);
    check("kill recupera los bloques de heap de la tarea (3 -> 0)", before == 3u && after == 0u);
    check("kill de idle o de un tid inexistente devuelve -1", task_kill(0) == -1 && task_kill(15) == -1);
    (void)me;
    mutex_destroy(&g_km);
}

/* ---- HU-E04-06 y costo ---- */

static void t_cost(void) {
    uint32_t t0 = cpu_cycles();
    for (uint32_t i = 0; i < 1000; i++) {
        mutex_lock(&g_mx);
        mutex_unlock(&g_mx);
    }
    uint32_t pair = (cpu_cycles() - t0) / 1000u;
    kprintf("mutex lock+unlock sin contencion = %u ciclos (%u us)\r\n", pair, cycles_to_us(pair));
    check("la seccion critica de lock+unlock dura < 50 us (2000 ciclos)", pair < 2000u);
    check("cada mutex tiene id unico", g_mx.id != 0u && g_mx.id != g_own.id);
}

void test_e04(void) {
    mutex_init(&g_mx, "datos");
    t_critical();
    t_race_and_mutex();
    t_ownership();
    t_fifo();
    t_timeouts();
    t_producer_consumer();
    t_sem_from_isr();
    t_deadlock();
    t_kill();
    t_cost();
    mutex_destroy(&g_mx);
    kprintf("fin de pruebas E04\r\n");
}
