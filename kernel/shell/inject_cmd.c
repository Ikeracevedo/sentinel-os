/* E09: control comun de escenarios. Los recursos pertenecen a las tareas de
 * cada escenario; stop mata primero TODAS sus tareas y despues deshace los
 * recursos compartidos. Ningun escenario puede matar la tarea del shell. */
#include <sentinel/shell.h>
#include <sentinel/task.h>
#include <sentinel/heap.h>
#include <sentinel/sync.h>
#include <sentinel/kprintf.h>
#include <sentinel/probe.h>
#include "../core/sched.h"
#include "../core/time.h"
#include "../arch/xtensa/cpu.h"

#define INJECT_MAX_TIDS 8u
#define LEAK_DEFAULT_BYTES 256u
#define LEAK_DEFAULT_INTERVAL_MS 500u
#define LEAK_MAX_BYTES 4096u
#define LEAK_MIN_INTERVAL_MS 10u
#define LEAK_HEAP_LIMIT_PERCENT 90u
#define FRAG_BLOCK_BYTES 3072u
#define FRAG_MAX_BLOCKS 64u
#define FRAG_HOLD_MS 30000u
#define HOG_RUN_MS 100u
#define HOG_STARVE_RUN_MS 1200u
#define HOG_STARVE_REST_MS 100u
#define HOG_REST_MS 2u
#define HOG_MAX_MS 20000u
#define PRIO_INV_LOW_WORK_CYCLES CPU_FREQ_HZ
#define PRIO_INV_MAX_MS 20000u
#define LOAD_MAX_WORKERS 8u
#define STACK_TEST_BYTES 3072u
#define STACK_TEST_MARGIN 1536u

typedef struct scenario scenario_t;
typedef int (*start_fn_t)(scenario_t *, int, char **);
typedef int (*cleanup_fn_t)(scenario_t *);

struct scenario {
    const char *name;
    const char *description;
    uint8_t id;                     /* EV_INJECT.arg0: identificador estable */
    uint8_t active;
    uint8_t count;
    uint8_t tids[INJECT_MAX_TIDS];
    uint32_t task_instances[INJECT_MAX_TIDS];
    const char *task_names[INJECT_MAX_TIDS];
    start_fn_t start;
    cleanup_fn_t cleanup;           /* libera recursos externos a las tareas */
};

static volatile uint32_t g_leak_bytes;
static volatile uint32_t g_leak_interval_ms;
static mutex_t g_dead_mutex[3];
static uint32_t g_dead_count;
static void *g_frag_blocks[FRAG_MAX_BLOCKS];
static uint32_t g_frag_cycle;
static volatile uint8_t g_hog_expired;
static mutex_t g_inv_mutex;
static volatile uint8_t g_inv_low_locked;
static volatile uint8_t g_inv_medium_expired;
static mutex_t g_load_mutex;
static volatile uint32_t g_load_iterations[LOAD_MAX_WORKERS];
static volatile uint32_t g_load_alloc_failures;
static uint32_t g_load_started_ms;
static uint32_t g_load_period_ms;
static uint32_t g_load_alloc_bytes;
static uint32_t g_load_hold_ms;
static uint32_t g_load_busy_ms;
static const char *g_load_profile;
static volatile uint32_t g_stack_depth;
static volatile uint32_t g_stack_sink;
static uint32_t g_stack_fault_before;
static const char *k_load_names[LOAD_MAX_WORKERS] = {
    "load_0", "load_1", "load_2", "load_3",
    "load_4", "load_5", "load_6", "load_7"
};
static const char *k_dead_names[3] = {"dead_a", "dead_b", "dead_c"};
static const char *k_dead_mutex_names[3] = {"dead_A", "dead_B", "dead_C"};

static int same(const char *a, const char *b) {
    while (*a != '\0' && *a == *b) {
        a++;
        b++;
    }
    return *a == *b;
}

/* Valida el limite durante la conversion: shell_parse_uint permite wrap de
 * uint32_t, indeseable en parametros que consumen memoria. */
static int parse_bounded(const char *text, uint32_t min, uint32_t max, uint32_t *out) {
    uint32_t value = 0u;
    if (text == NULL || *text == '\0') {
        return -1;
    }
    for (; *text != '\0'; text++) {
        if (*text < '0' || *text > '9') {
            return -1;
        }
        uint32_t digit = (uint32_t)(*text - '0');
        if (value > (max - digit) / 10u) {
            return -1;
        }
        value = value * 10u + digit;
    }
    if (value < min) {
        return -1;
    }
    *out = value;
    return 0;
}

static void leaker_task(void *arg) {
    (void)arg;
    for (;;) {
        heap_stats_t stats;
        heap_stats(&stats);
        /* Se detiene la asignacion antes de agotar el heap. La tarea sigue
         * viva para que stop pueda liberar todos los bloques de su TID. */
        if (stats.used < (HEAP_SIZE / 10u) * LEAK_HEAP_LIMIT_PERCENT &&
            g_leak_bytes <= stats.free) {
            (void)kmalloc(g_leak_bytes);
        }
        task_sleep(g_leak_interval_ms);
    }
}

static void dead_task(void *arg) {
    uint32_t index = (uint32_t)(uintptr_t)arg;
    mutex_t *first = &g_dead_mutex[index];
    mutex_t *second = &g_dead_mutex[(index + 1u) % g_dead_count];
    if (mutex_lock(first) != 0) {
        return;
    }
    /* Todas toman su primer mutex antes de intentar el segundo. */
    task_sleep(50u);
    if (mutex_lock(second) == 0) {
        /* Solo ocurre si alguien deshizo el ciclo por fuera de inject stop. */
        (void)mutex_unlock(second);
    }
    (void)mutex_unlock(first);
}

static void frag_task(void *arg) {
    (void)arg;
    for (;;) {
        uint32_t count = 0u;
        while (count < FRAG_MAX_BLOCKS) {
            void *p = kmalloc(FRAG_BLOCK_BYTES);
            if (p == NULL) {
                break;
            }
            g_frag_blocks[count++] = p;
        }
        for (uint32_t i = 0; i < count; i += 2u) {
            kfree(g_frag_blocks[i]);
            g_frag_blocks[i] = NULL;
        }

        heap_stats_t stats;
        heap_stats(&stats);
        void *probe = kmalloc(4096u);
        if (probe != NULL) {
            kfree(probe);
        }
        g_frag_cycle++;
        kprintf("inject frag: ciclo=%u bloques=%u libre=%u mayor=%u frag=%u%% kmalloc(4096)=%s\r\n",
                g_frag_cycle, count, stats.free, stats.largest_free_block,
                heap_frag_percent(&stats), probe == NULL ? "FALLO" : "EXITO");

        /* Mantiene el patron visible. Luego limpia los bloques supervivientes
         * y lo reproduce sin aleatoriedad. stop puede matar la tarea en
         * cualquier punto y heap_free_owned recupera todos los que queden. */
        task_sleep(FRAG_HOLD_MS);
        for (uint32_t i = 1u; i < count; i += 2u) {
            kfree(g_frag_blocks[i]);
            g_frag_blocks[i] = NULL;
        }
    }
}

static void hog_task(void *arg) {
    (void)arg;
    uint32_t started = uptime_ms();
    while ((uint32_t)(uptime_ms() - started) < HOG_MAX_MS) {
        uint32_t slice = uptime_ms();
        /* Bucle de CPU real, no una tarea que simplemente duerme. La pausa
         * breve permite que el shell y el tracer transmitan inject stop. */
        while ((uint32_t)(uptime_ms() - slice) < HOG_STARVE_RUN_MS &&
               (uint32_t)(uptime_ms() - started) < HOG_MAX_MS) {
            __asm__ __volatile__("" ::: "memory");
        }
        task_sleep(HOG_STARVE_REST_MS);
    }
    g_hog_expired = 1u;
    kprintf("inject hog: limite de %u ms alcanzado; tarea terminada\r\n", HOG_MAX_MS);
}

static int start_hog(scenario_t *scenario, int argc, char **argv) {
    uint32_t priority = 5u; /* shell=4, tracer=6: ambos pueden demostrar progreso */
    if (argc > 3 ||
        (argc == 3 && parse_bounded(argv[2], 3u, 5u, &priority) != 0)) {
        shell_print_error("inject", "uso: inject hog [prioridad 3..5]", NULL);
        return 1;
    }
    g_hog_expired = 0u;
    int32_t tid = task_create("hog", hog_task, NULL, (uint8_t)priority, 0u);
    if (tid < 0) {
        shell_print_error("inject", "no se pudo crear hog (tabla llena)", NULL);
        return 1;
    }
    scenario->tids[0] = (uint8_t)tid;
    scenario->task_instances[0] = task_get((uint32_t)tid)->instance_id;
    scenario->task_names[0] = "hog";
    scenario->count = 1u;
    kprintf("inject hog: tarea tid=%u prioridad=%u; pausa %u ms cada %u ms; limite %u ms\r\n",
            (uint32_t)tid, priority, HOG_STARVE_REST_MS, HOG_STARVE_RUN_MS, HOG_MAX_MS);
    return 0;
}

/* La tarea baja necesita un segundo de CPU propio para terminar la seccion
 * critica. La media no usa el mutex: al apropiarse de la CPU impide que baja
 * acumule ese segundo y por eso alta permanece bloqueada indirectamente. */
static void inv_low_task(void *arg) {
    (void)arg;
    if (mutex_lock(&g_inv_mutex) != 0) {
        return;
    }
    g_inv_low_locked = 1u;
    task_sleep(50u);
    tcb_t *self = sched_current();
    uint64_t start_cycles = self->cpu_cycles;
    while (self->cpu_cycles - start_cycles < PRIO_INV_LOW_WORK_CYCLES) {
        __asm__ __volatile__("" ::: "memory");
    }
    (void)mutex_unlock(&g_inv_mutex);
}

static void inv_high_task(void *arg) {
    (void)arg;
    if (mutex_lock(&g_inv_mutex) == 0) {
        (void)mutex_unlock(&g_inv_mutex);
    }
}

static void inv_medium_task(void *arg) {
    (void)arg;
    uint32_t started = uptime_ms();
    while ((uint32_t)(uptime_ms() - started) < PRIO_INV_MAX_MS) {
        uint32_t slice = uptime_ms();
        while ((uint32_t)(uptime_ms() - slice) < HOG_RUN_MS &&
               (uint32_t)(uptime_ms() - started) < PRIO_INV_MAX_MS) {
            __asm__ __volatile__("" ::: "memory");
        }
        task_sleep(HOG_REST_MS);
    }
    g_inv_medium_expired = 1u;
    kprintf("inject prio-inv: limite de %u ms alcanzado\r\n", PRIO_INV_MAX_MS);
}

static int cleanup_prio_inv(scenario_t *scenario) {
    (void)scenario;
    return mutex_destroy(&g_inv_mutex);
}

static int start_prio_inv(scenario_t *scenario, int argc, char **argv) {
    (void)argv;
    if (argc != 2) {
        shell_print_error("inject", "uso: inject prio-inv", NULL);
        return 1;
    }
    g_inv_low_locked = 0u;
    g_inv_medium_expired = 0u;
    mutex_init(&g_inv_mutex, "prio_M");
    int32_t tid = task_create("baja", inv_low_task, NULL, 7u, 0u);
    if (tid < 0) {
        (void)mutex_destroy(&g_inv_mutex);
        shell_print_error("inject", "no se pudo crear baja", NULL);
        return 1;
    }
    scenario->tids[0] = (uint8_t)tid;
    scenario->task_instances[0] = task_get((uint32_t)tid)->instance_id;
    scenario->task_names[0] = "baja";
    scenario->count = 1u;

    /* El shell duerme para que baja, de prioridad 7, tome M antes de que
     * exista alta. Asi el orden no depende de una carrera del scheduler. */
    for (uint32_t i = 0; i < 20u && !g_inv_low_locked; i++) {
        task_sleep(10u);
    }
    if (!g_inv_low_locked) {
        (void)task_kill((uint32_t)tid);
        scenario->count = 0u;
        (void)mutex_destroy(&g_inv_mutex);
        shell_print_error("inject", "baja no alcanzo a tomar el mutex", NULL);
        return 1;
    }

    tid = task_create("alta", inv_high_task, NULL, 1u, 0u);
    if (tid < 0) {
        (void)task_kill(scenario->tids[0]);
        scenario->count = 0u;
        (void)mutex_destroy(&g_inv_mutex);
        shell_print_error("inject", "no se pudo crear alta", NULL);
        return 1;
    }
    scenario->tids[1] = (uint8_t)tid;
    scenario->task_instances[1] = task_get((uint32_t)tid)->instance_id;
    scenario->task_names[1] = "alta";
    scenario->count = 2u;

    tid = task_create("media", inv_medium_task, NULL, 4u, 0u);
    if (tid < 0) {
        (void)task_kill(scenario->tids[1]);
        (void)task_kill(scenario->tids[0]);
        scenario->count = 0u;
        (void)mutex_destroy(&g_inv_mutex);
        shell_print_error("inject", "no se pudo crear media", NULL);
        return 1;
    }
    scenario->tids[2] = (uint8_t)tid;
    scenario->task_instances[2] = task_get((uint32_t)tid)->instance_id;
    scenario->task_names[2] = "media";
    scenario->count = 3u;
    kprintf("inject prio-inv: baja(7) posee M, alta(1) espera M, media(4) ocupa CPU; limite %u ms\r\n",
            PRIO_INV_MAX_MS);
    return 0;
}

static void load_task(void *arg) {
    uint32_t index = (uint32_t)(uintptr_t)arg;
    uint32_t next = uptime_ms();
    for (;;) {
        /* Trabajo de CPU y cadencia absolutos: los mismos pasos en cada
         * ejecucion, sin numeros aleatorios ni dependencia del TID. */
        uint32_t busy_start = uptime_ms();
        while ((uint32_t)(uptime_ms() - busy_start) < g_load_busy_ms) {
            __asm__ __volatile__("" ::: "memory");
        }
        void *block = kmalloc(g_load_alloc_bytes);
        if (block == NULL) {
            g_load_alloc_failures++;
        }
        if (mutex_lock(&g_load_mutex) == 0) {
            task_sleep(g_load_hold_ms);
            (void)mutex_unlock(&g_load_mutex);
        }
        if (block != NULL) {
            kfree(block);
        }
        g_load_iterations[index]++;
        next += g_load_period_ms;
        uint32_t now = uptime_ms();
        if ((int32_t)(next - now) > 0) {
            task_sleep(next - now);
        } else {
            next = now;
            task_sleep(1u);
        }
    }
}

static int cleanup_load(scenario_t *scenario) {
    (void)scenario;
    return mutex_destroy(&g_load_mutex);
}

static int start_load(scenario_t *scenario, int argc, char **argv) {
    if (argc != 3) {
        shell_print_error("inject", "uso: inject load light|nominal|heavy", NULL);
        return 1;
    }
    uint32_t workers;
    if (same(argv[2], "light")) {
        g_load_profile = "light";
        workers = 2u;
        g_load_period_ms = 200u;
        g_load_alloc_bytes = 32u;
        g_load_hold_ms = 1u;
        g_load_busy_ms = 0u;
    } else if (same(argv[2], "nominal")) {
        g_load_profile = "nominal";
        workers = 5u;
        g_load_period_ms = 100u;
        g_load_alloc_bytes = 64u;
        g_load_hold_ms = 1u;
        g_load_busy_ms = 1u;
    } else if (same(argv[2], "heavy")) {
        g_load_profile = "heavy";
        workers = 8u;
        g_load_period_ms = 50u;
        g_load_alloc_bytes = 128u;
        g_load_hold_ms = 2u;
        g_load_busy_ms = 2u;
    } else {
        shell_print_error("inject", "perfil desconocido (light|nominal|heavy)", argv[2]);
        return 1;
    }
    g_load_alloc_failures = 0u;
    for (uint32_t i = 0; i < LOAD_MAX_WORKERS; i++) {
        g_load_iterations[i] = 0u;
    }
    mutex_init(&g_load_mutex, "load_M");
    g_load_started_ms = uptime_ms();
    for (uint32_t i = 0; i < workers; i++) {
        int32_t tid = task_create(k_load_names[i], load_task,
                                  (void *)(uintptr_t)i, 5u, 0u);
        if (tid < 0) {
            while (scenario->count > 0u) {
                (void)task_kill(scenario->tids[--scenario->count]);
            }
            (void)mutex_destroy(&g_load_mutex);
            shell_print_error("inject", "no hay ranuras para el perfil de carga", NULL);
            return 1;
        }
        scenario->tids[i] = (uint8_t)tid;
        scenario->task_instances[i] = task_get((uint32_t)tid)->instance_id;
        scenario->task_names[i] = k_load_names[i];
        scenario->count++;
    }
    kprintf("inject load %s: %u tareas pri=5, periodo=%u ms, alloc/free=%u B, mutex=%u ms, CPU=%u ms\r\n",
            g_load_profile, workers, g_load_period_ms, g_load_alloc_bytes,
            g_load_hold_ms, g_load_busy_ms);
    return 0;
}

/* Recursion real, pero el ultimo marco conserva espacio para la interrupcion
 * y el scheduler. Se corrompe solamente el canario de ESTA tarea: nunca se
 * escribe fuera de su pila ni se alcanza la pila de una tarea vecina. */
static void __attribute__((noinline)) stack_recurse(uint32_t depth) {
    volatile uint8_t frame[96];
    for (uint32_t i = 0; i < sizeof(frame); i++) {
        frame[i] = (uint8_t)(depth + i);
    }
    g_stack_depth = depth;
    tcb_t *self = sched_current();
    if ((uintptr_t)&frame[0] <=
        (uintptr_t)self->stack_base + STACK_TEST_MARGIN) {
        *(volatile uint32_t *)self->stack_base = STACK_CANARY ^ 1u;
        sched_yield();
        return; /* solo si el scheduler no detecto el canario */
    }
    stack_recurse(depth + 1u);
    /* El acceso posterior a la llamada impide convertirla en salto de cola. */
    g_stack_sink += frame[depth % sizeof(frame)];
}

static void stack_overflow_task(void *arg) {
    (void)arg;
    stack_recurse(1u);
}

static int start_stack_overflow(scenario_t *scenario, int argc, char **argv) {
    (void)argv;
    if (argc != 2) {
        shell_print_error("inject", "uso: inject stack-overflow", NULL);
        return 1;
    }
    g_stack_depth = 0u;
    g_stack_fault_before = sched_recovered_stack_faults();
    int32_t tid = task_create("stack_ovf", stack_overflow_task, NULL, 5u,
                              STACK_TEST_BYTES);
    if (tid < 0) {
        shell_print_error("inject", "no se pudo crear stack_ovf", NULL);
        return 1;
    }
    tcb_t *t = task_get((uint32_t)tid);
    t->recover_stack_fault = 1u;
    scenario->tids[0] = (uint8_t)tid;
    scenario->task_instances[0] = t->instance_id;
    scenario->task_names[0] = "stack_ovf";
    scenario->count = 1u;
    kprintf("inject stack-overflow: tarea stack_ovf tid=%u, pila=%u B; recursion controlada\r\n",
            (uint32_t)tid, STACK_TEST_BYTES);
    return 0;
}

static int start_frag(scenario_t *scenario, int argc, char **argv) {
    (void)argv;
    if (argc != 2) {
        shell_print_error("inject", "uso: inject frag", NULL);
        return 1;
    }
    g_frag_cycle = 0u;
    int32_t tid = task_create("fragger", frag_task, NULL, 5u, 0u);
    if (tid < 0) {
        shell_print_error("inject", "no se pudo crear fragger (tabla llena)", NULL);
        return 1;
    }
    scenario->tids[0] = (uint8_t)tid;
    scenario->task_instances[0] = task_get((uint32_t)tid)->instance_id;
    scenario->task_names[0] = "fragger";
    scenario->count = 1u;
    kprintf("inject frag: tarea fragger tid=%u; patron fijo de %u B, ciclo cada %u ms\r\n",
            (uint32_t)tid, FRAG_BLOCK_BYTES, FRAG_HOLD_MS);
    return 0;
}

static int cleanup_deadlock(scenario_t *scenario) {
    (void)scenario;
    int rc = 0;
    for (uint32_t i = 0; i < g_dead_count; i++) {
        if (mutex_destroy(&g_dead_mutex[i]) != 0) {
            rc = -1;
        }
    }
    return rc;
}

static int start_deadlock(scenario_t *scenario, int argc, char **argv) {
    uint32_t n = 2u;
    if (argc > 3 || (argc == 3 && parse_bounded(argv[2], 2u, 3u, &n) != 0)) {
        shell_print_error("inject", "uso: inject deadlock [2|3]", NULL);
        return 1;
    }
    g_dead_count = n;
    for (uint32_t i = 0; i < n; i++) {
        mutex_init(&g_dead_mutex[i], k_dead_mutex_names[i]);
    }
    for (uint32_t i = 0; i < n; i++) {
        int32_t tid = task_create(k_dead_names[i], dead_task,
                                  (void *)(uintptr_t)i, 5u, 0u);
        if (tid < 0) {
            /* No publicar un escenario a medias. El shell tiene prioridad 4,
             * asi que ninguna tarea de prioridad 5 corre durante este rollback. */
            while (scenario->count > 0u) {
                (void)task_kill(scenario->tids[--scenario->count]);
            }
            (void)cleanup_deadlock(scenario);
            shell_print_error("inject", "no hay ranuras para todas las tareas del deadlock", NULL);
            return 1;
        }
        scenario->tids[scenario->count] = (uint8_t)tid;
        scenario->task_instances[scenario->count] = task_get((uint32_t)tid)->instance_id;
        scenario->task_names[scenario->count] = k_dead_names[i];
        scenario->count++;
    }
    kprintf("inject deadlock: %u tareas, %u mutexes; bloqueo tras 50 ms\r\n", n, n);
    return 0;
}

static int start_leak(scenario_t *scenario, int argc, char **argv) {
    uint32_t bytes = LEAK_DEFAULT_BYTES;
    uint32_t interval = LEAK_DEFAULT_INTERVAL_MS;
    if (argc > 4 ||
        (argc >= 3 && parse_bounded(argv[2], 1u, LEAK_MAX_BYTES, &bytes) != 0) ||
        (argc >= 4 && parse_bounded(argv[3], LEAK_MIN_INTERVAL_MS, 60000u, &interval) != 0)) {
        shell_print_error("inject", "uso: inject leak [bytes 1..4096] [intervalo_ms 10..60000]", NULL);
        return 1;
    }
    g_leak_bytes = bytes;
    g_leak_interval_ms = interval;
    int32_t tid = task_create("leaker", leaker_task, NULL, 5u, 0u);
    if (tid < 0) {
        shell_print_error("inject", "no se pudo crear leaker (tabla llena)", NULL);
        return 1;
    }
    scenario->tids[0] = (uint8_t)tid;
    scenario->task_instances[0] = task_get((uint32_t)tid)->instance_id;
    scenario->task_names[0] = "leaker";
    scenario->count = 1u;
    kprintf("inject leak: tarea leaker tid=%u, %u B cada %u ms (limite: 90%% del heap)\r\n",
            (uint32_t)tid, bytes, interval);
    return 0;
}

static scenario_t g_scenarios[] = {
    {.name="deadlock", .description="ciclo real de espera entre mutexes", .id=1u,
     .start=start_deadlock, .cleanup=cleanup_deadlock},
    {.name="leak", .description="reservas periodicas de heap", .id=2u,
     .start=start_leak},
    {.name="frag", .description="fragmentacion del heap", .id=3u,
     .start=start_frag},
    {.name="hog", .description="tarea que acapara CPU", .id=4u,
     .start=start_hog},
    {.name="prio-inv", .description="inversion de prioridad", .id=5u,
     .start=start_prio_inv, .cleanup=cleanup_prio_inv},
    {.name="load", .description="carga sintetica reproducible", .id=6u,
     .start=start_load, .cleanup=cleanup_load},
    {.name="stack-overflow", .description="desbordamiento de pila", .id=7u,
     .start=start_stack_overflow},
};
#define SCENARIO_COUNT (sizeof(g_scenarios) / sizeof(g_scenarios[0]))

static scenario_t *find_scenario(const char *name) {
    for (uint32_t i = 0; i < SCENARIO_COUNT; i++) {
        if (same(name, g_scenarios[i].name)) {
            return &g_scenarios[i];
        }
    }
    return NULL;
}

static int stop_scenario(scenario_t *s);

static void reconcile_hog(scenario_t *s) {
    if (s->id != 4u || !s->active || !g_hog_expired) {
        return;
    }
    tcb_t *t = task_get(s->tids[0]);
    if (t == NULL || t->instance_id != s->task_instances[0] ||
        !same(t->name, "hog")) {
        s->count = 0u;
        s->active = 0u;
        kprintf("inject hog: apagado automatico completado\r\n");
    }
}

static void reconcile_prio_inv(scenario_t *s) {
    if (s->id == 5u && s->active && g_inv_medium_expired) {
        kprintf("inject prio-inv: apagado automatico; liberando mutex y tareas\r\n");
        (void)stop_scenario(s);
    }
}

static void reconcile_stack_overflow(scenario_t *s) {
    if (s->id != 7u || !s->active) {
        return;
    }
    tcb_t *t = task_get(s->tids[0]);
    if (t != NULL && t->instance_id == s->task_instances[0] &&
        same(t->name, s->task_names[0])) {
        return;
    }
    s->count = 0u;
    s->active = 0u;
    if (sched_recovered_stack_faults() != g_stack_fault_before) {
        kprintf("DESBORDAMIENTO DE PILA: tarea 'stack_ovf' (tid=%u), profundidad=%u; solo ella fue terminada\r\n",
                (uint32_t)s->tids[0], g_stack_depth);
    } else {
        kprintf("inject stack-overflow: tarea terminada sin deteccion de canario\r\n");
    }
}

static void list_scenarios(void) {
    kprintf("Escenarios:\r\n");
    for (uint32_t i = 0; i < SCENARIO_COUNT; i++) {
        scenario_t *s = &g_scenarios[i];
        kprintf("  %s: %s [%s]\r\n", s->name, s->description,
                s->start ? "disponible" : "pendiente");
    }
}

static void status_scenarios(void) {
    uint32_t active = 0u;
    for (uint32_t i = 0; i < SCENARIO_COUNT; i++) {
        scenario_t *s = &g_scenarios[i];
        reconcile_hog(s);
        reconcile_prio_inv(s);
        reconcile_stack_overflow(s);
        if (!s->active) {
            continue;
        }
        active++;
        kprintf("inject: %s activo; tareas:", s->name);
        for (uint32_t j = 0; j < s->count; j++) {
            kprintf(" %u", (uint32_t)s->tids[j]);
        }
        kprintf("\r\n");
        if (s->id == 6u) {
            uint32_t iterations = 0u;
            for (uint32_t j = 0; j < s->count; j++) {
                iterations += g_load_iterations[j];
            }
            uint32_t elapsed = uptime_ms() - g_load_started_ms;
            uint32_t rate_x10 = elapsed > 0u ? (iterations * 10000u) / elapsed : 0u;
            kprintf("load: perfil=%s tiempo=%u ms ciclos=%u tasa=%u.%u/s fallos_alloc=%u\r\n",
                    g_load_profile, elapsed, iterations, rate_x10 / 10u,
                    rate_x10 % 10u, g_load_alloc_failures);
        }
    }
    if (active == 0u) {
        kprintf("inject: sin escenarios activos\r\n");
    }
}

static int stop_scenario(scenario_t *s) {
    if (!s->active) {
        kprintf("inject stop: %s no esta activo\r\n", s->name);
        return 0;
    }
    /* Matar en orden inverso: un propietario no debe traspasar el mutex a
     * un esperador que ya no podremos controlar. */
    for (uint32_t j = s->count; j > 0u; j--) {
        uint32_t tid = s->tids[j - 1u];
        tcb_t *t = task_get(tid);
        if (t == NULL || t->instance_id != s->task_instances[j - 1u] ||
            !same(t->name, s->task_names[j - 1u])) {
            /* kill externo ya libero sus bloques. Si se reciclo el TID,
             * JAMAS matar la tarea ajena que ocupa ahora la ranura. */
            s->count--;
            continue;
        }
        if (tid == shell_tid() || tid == 0u) {
            shell_print_error("inject stop", "TID protegido; no se mato", s->name);
            return 1;
        }
        if (task_kill(tid) != 0) {
            shell_print_error("inject stop", "no se pudo terminar una tarea", s->name);
            return 1;
        }
        s->count--;
    }
    if (s->cleanup != NULL && s->cleanup(s) != 0) {
        shell_print_error("inject stop", "no se pudieron liberar los recursos", s->name);
        return 1;
    }
    s->active = 0u;
    kprintf("inject stop: %s detenido y limpio\r\n", s->name);
    return 0;
}

static int cmd_inject(int argc, char **argv) {
    if (argc == 1 || (argc == 2 && same(argv[1], "list"))) {
        list_scenarios();
        return 0;
    }
    if (argc == 2 && same(argv[1], "status")) {
        status_scenarios();
        return 0;
    }
    if (same(argv[1], "stop")) {
        if (argc > 3) {
            shell_print_error("inject", "uso: inject stop [escenario]", NULL);
            return 1;
        }
        if (argc == 3) {
            scenario_t *s = find_scenario(argv[2]);
            if (s == NULL) {
                shell_print_error("inject", "escenario desconocido", argv[2]);
                return 1;
            }
            reconcile_stack_overflow(s);
            return stop_scenario(s);
        }
        int rc = 0;
        for (uint32_t i = 0; i < SCENARIO_COUNT; i++) {
            reconcile_stack_overflow(&g_scenarios[i]);
            if (g_scenarios[i].active && stop_scenario(&g_scenarios[i]) != 0) {
                rc = 1;
            }
        }
        if (rc == 0) {
            kprintf("inject stop: no quedan escenarios activos\r\n");
        }
        return rc;
    }
    scenario_t *s = find_scenario(argv[1]);
    if (s == NULL) {
        shell_print_error("inject", "escenario desconocido (use inject list)", argv[1]);
        return 1;
    }
    if (s->start == NULL) {
        shell_print_error("inject", "escenario pendiente de implementacion", s->name);
        return 1;
    }
    reconcile_hog(s);
    reconcile_prio_inv(s);
    reconcile_stack_overflow(s);
    if (s->active) {
        shell_print_error("inject", "escenario ya activo; detengalo primero", s->name);
        return 1;
    }
    if (s->start(s, argc, argv) != 0) {
        return 1;
    }
    s->active = 1u;
#if SENTINEL_TRACE
    /* arg0 = escenario; arg1 = numero de tareas creadas. */
    trace_probe_emit(EV_INJECT, shell_tid(), s->id, s->count);
#endif
    return 0;
}

void inject_register_command(void) {
    cmd_register("inject", cmd_inject, "activa y detiene fallos controlados",
                 "inject list|status|stop [escenario]|<escenario> [parametros]");
}
