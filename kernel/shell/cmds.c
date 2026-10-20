/* Comandos incorporados del shell: sistema de archivos, sistema y control de tareas.
 * Cada uno es int cmd_xxx(int argc, char **argv) y se registra en la tabla. Los mensajes de
 * error son siempre texto legible: "cd: no existe el directorio: /hoem", nunca "-2". */

#include <sentinel/shell.h>
#include <sentinel/vfs.h>
#include <sentinel/procfs.h>
#include <sentinel/heap.h>
#include <sentinel/task.h>
#include <sentinel/sync.h>
#include <sentinel/graph.h>
#include <sentinel/detector.h>
#include <sentinel/alerts.h>
#include <sentinel/kprintf.h>
#include <sentinel/probe.h>
#if SENTINEL_TRANSPORT
#include <sentinel/transport.h>
#endif
#include "../drivers/uart.h"
#include "../drivers/gpio.h"
#include "../core/sched.h"
#include "../core/time.h"
#include "../arch/xtensa/cpu.h"

void inject_register_command(void);

/* ---- utilidades ---- */

static uint32_t str_len(const char *s) {
    uint32_t n = 0;
    while (s[n] != '\0') {
        n++;
    }
    return n;
}

static int str_eq(const char *a, const char *b) {
    while (*a != '\0' && *a == *b) {
        a++;
        b++;
    }
    return *a == *b;
}

/* Imprime texto convirtiendo '\n' en "\r\n" (la terminal serie lo necesita). */
static void put_text(const char *buf, uint32_t n) {
    for (uint32_t i = 0; i < n; i++) {
        if (buf[i] == '\n') {
            uart_putc('\r');
        }
        uart_putc(buf[i]);
    }
}

/* Mensaje a partir de un codigo del VFS: "cmd: <texto>: ruta". */
static int vfs_fail(const char *cmd, int rc, const char *path) {
    shell_print_error(cmd, vfs_strerror(rc), path);
    return 1;
}

/* ms = ciclos / 40000 sin division de 64 bits (2^32 / 40000 ~= 107374). */
static uint32_t cycles_to_ms64(uint64_t c) {
    return (uint32_t)(c >> 32) * 107374u + (uint32_t)c / 40000u;
}

/* ---- sistema de archivos ---- */

static int cmd_pwd(int argc, char **argv) {
    (void)argc;
    (void)argv;
    char buf[VFS_PATH_MAX];
    if (vfs_getcwd(buf, sizeof(buf)) != VFS_OK) {
        shell_print_error("pwd", "ruta demasiado larga", NULL);
        return 1;
    }
    kprintf("%s\r\n", buf);
    return 0;
}

static int cmd_cd(int argc, char **argv) {
    const char *target = (argc >= 2) ? argv[1] : "/home/iker";
    int rc = vfs_chdir(target);
    if (rc == VFS_ENOENT) {
        shell_print_error("cd", "no existe el directorio", target);
        return 1;
    }
    if (rc == VFS_ENOTDIR) {
        shell_print_error("cd", "no es un directorio", target);
        return 1;
    }
    if (rc != VFS_OK) {
        return vfs_fail("cd", rc, target);
    }
    return 0;
}

static void print_entry(const vnode_t *n) {
    char perm[5];
    perm[0] = (n->type == VN_DIR) ? 'd' : '-';
    perm[1] = (n->perms & VFS_PERM_R) ? 'r' : '-';
    perm[2] = (n->perms & VFS_PERM_W) ? 'w' : '-';
    perm[3] = (n->perms & VFS_PERM_X) ? 'x' : '-';
    perm[4] = '\0';

    char size[12];
    if (n->type == VN_DIR) {
        size[0] = '-';
        size[1] = '-';
        size[2] = '\0';
    } else {
        uint32_t v = n->size;
        char tmp[11];
        uint32_t k = 0;
        if (v == 0u) {
            tmp[k++] = '0';
        }
        while (v > 0u) {
            tmp[k++] = (char)('0' + (v % 10u));
            v /= 10u;
        }
        uint32_t j = 0;
        while (k > 0u) {
            size[j++] = tmp[--k];
        }
        size[j] = '\0';
    }
    uint32_t pad = 6u - str_len(size);
    kprintf("%s ", perm);
    for (uint32_t i = 0; i < pad; i++) {
        uart_putc(' ');
    }
    kprintf("%s  %s%s\r\n", size, n->name, (n->type == VN_DIR) ? "/" : "");
}

static int cmd_ls(int argc, char **argv) {
    const char *path = (argc >= 2) ? argv[1] : ".";
    vnode_t *n = vfs_resolve(path, vfs_cwd());
    if (n == NULL) {
        shell_print_error("ls", "no existe", path);
        return 1;
    }
    if (n->type != VN_DIR) {
        print_entry(n);                     /* ls sobre un archivo: solo ese archivo */
        return 0;
    }
    for (uint32_t i = 0;; i++) {
        vnode_t *c = vfs_readdir(n, i);
        if (c == NULL) {
            break;
        }
        print_entry(c);
    }
    return 0;
}

static int cmd_cat(int argc, char **argv) {
    if (argc < 2) {
        shell_print_error("cat", "falta el archivo", NULL);
        return 1;
    }
    int fd = vfs_open(argv[1], VFS_O_RDONLY, vfs_cwd());
    if (fd == VFS_EISDIR) {
        shell_print_error("cat", "es un directorio", argv[1]);
        return 1;
    }
    if (fd == VFS_ENOENT) {
        shell_print_error("cat", "no existe", argv[1]);
        return 1;
    }
    if (fd < 0) {
        return vfs_fail("cat", fd, argv[1]);
    }
    char buf[64];
    int n;
    int last_nl = 1;
    while ((n = vfs_read_fd(fd, buf, sizeof(buf))) > 0) {
        put_text(buf, (uint32_t)n);
        last_nl = (buf[n - 1] == '\n');
    }
    if (!last_nl) {
        kprintf("\r\n");
    }
    vfs_close(fd);
    return 0;
}

static int cmd_mkdir(int argc, char **argv) {
    if (argc < 2) {
        shell_print_error("mkdir", "falta el nombre", NULL);
        return 1;
    }
    int rc = vfs_mkdir(argv[1], vfs_cwd());
    if (rc == VFS_EEXIST) {
        shell_print_error("mkdir", "ya existe", argv[1]);
        return 1;
    }
    if (rc == VFS_ENOENT) {
        shell_print_error("mkdir", "no existe el directorio padre de", argv[1]);
        return 1;
    }
    return (rc == VFS_OK) ? 0 : vfs_fail("mkdir", rc, argv[1]);
}

static int cmd_touch(int argc, char **argv) {
    if (argc < 2) {
        shell_print_error("touch", "falta el nombre", NULL);
        return 1;
    }
    if (vfs_resolve(argv[1], vfs_cwd()) != NULL) {
        return 0;                           /* ya existe: no hace nada */
    }
    int rc = vfs_create(argv[1], vfs_cwd());
    return (rc == VFS_OK) ? 0 : vfs_fail("touch", rc, argv[1]);
}

static int cmd_write(int argc, char **argv) {
    if (argc < 3) {
        shell_print_error("write", "uso: write <archivo> <texto...>", NULL);
        return 1;
    }
    int fd = vfs_open(argv[1], VFS_O_WRONLY | VFS_O_CREAT | VFS_O_TRUNC, vfs_cwd());
    if (fd < 0) {
        return vfs_fail("write", fd, argv[1]);
    }
    int rc = 0;
    for (int i = 2; i < argc && rc >= 0; i++) {
        if (i > 2) {
            rc = vfs_write_fd(fd, " ", 1);
        }
        if (rc >= 0) {
            rc = vfs_write_fd(fd, argv[i], str_len(argv[i]));
        }
    }
    if (rc >= 0) {
        rc = vfs_write_fd(fd, "\n", 1);
    }
    vfs_close(fd);
    return (rc >= 0) ? 0 : vfs_fail("write", rc, argv[1]);
}

static int cmd_rm(int argc, char **argv) {
    int recursive = 0;
    const char *path = NULL;
    for (int i = 1; i < argc; i++) {
        if (str_eq(argv[i], "-r")) {
            recursive = 1;
        } else {
            path = argv[i];
        }
    }
    if (path == NULL) {
        shell_print_error("rm", "falta el nombre", NULL);
        return 1;
    }
    vnode_t *n = vfs_resolve(path, vfs_cwd());
    if (n == NULL) {
        shell_print_error("rm", "no existe", path);
        return 1;
    }
    int rc;
    if (recursive) {
        rc = vfs_rm_recursive(path, vfs_cwd());
    } else if (n->type == VN_DIR) {
        rc = vfs_rmdir(path, vfs_cwd());
        if (rc == VFS_ENOTEMPTY) {
            shell_print_error("rm", "el directorio no esta vacio (use rm -r)", path);
            return 1;
        }
    } else {
        rc = vfs_unlink(path, vfs_cwd());
    }
    if (rc == VFS_EBUSY) {
        shell_print_error("rm", "en uso (abierto o es el directorio actual de una tarea)", path);
        return 1;
    }
    return (rc == VFS_OK) ? 0 : vfs_fail("rm", rc, path);
}

/* ---- sistema ---- */

static int cmd_echo(int argc, char **argv) {
    for (int i = 1; i < argc; i++) {
        if (i > 1) {
            uart_putc(' ');
        }
        kprintf("%s", argv[i]);
    }
    kprintf("\r\n");
    return 0;
}

static int cmd_clear(int argc, char **argv) {
    (void)argc;
    (void)argv;
    kprintf("\033[2J\033[H");
    return 0;
}

static int cmd_uptime(int argc, char **argv) {
    (void)argc;
    (void)argv;
    char buf[96];
    uptime_text(buf, sizeof(buf));
    put_text(buf, str_len(buf));
    return 0;
}

static int cmd_uname(int argc, char **argv) {
    char buf[128];
    if (argc >= 2 && str_eq(argv[1], "-a")) {
        version_text(buf, sizeof(buf));
        put_text(buf, str_len(buf));
        return 0;
    }
    kprintf("Sentinel OS\r\n");
    return 0;
}

static int cmd_free(int argc, char **argv) {
    char buf[320];
    meminfo_text(buf, sizeof(buf));
    put_text(buf, str_len(buf));
    if (argc >= 2 && str_eq(argv[1], "-v")) {
        heap_dump();
    }
    return 0;
}

static int cmd_sync(int argc, char **argv) {
    (void)argc;
    (void)argv;
    sync_dump();
    return 0;
}

static int cmd_graph(int argc, char **argv) {
    (void)argc;
    (void)argv;
    graph_dump();
    return 0;
}

static int cmd_deadlock(int argc, char **argv) {
    (void)argv;
    if (argc != 1) {
        shell_print_error("deadlock", "uso: deadlock", NULL);
        return 1;
    }
    graph_t graph;
    graph_cycle_t cycle;
    uint32_t capture_cycles = graph_build(&graph);
    if (!graph_find_cycle(&graph, &cycle)) {
        kprintf("deadlock: sin ciclo; captura=%u ciclos\r\n", capture_cycles);
        return 0;
    }
    kprintf("deadlock: ciclo=");
    for (uint32_t i = 0u; i < cycle.count; i++) {
        kprintf("%u -> ", (uint32_t)cycle.tid[i]);
    }
    kprintf("%u; tareas=%u captura=%u ciclos\r\n",
            (uint32_t)cycle.tid[0], (uint32_t)cycle.count,
            capture_cycles);
    return 0;
}

static int cmd_detector(int argc, char **argv) {
    if (argc != 1) {
        uint32_t threshold;
        if (argc != 3 || shell_parse_uint(argv[2], &threshold) != 0 ||
            !(str_eq(argv[1], "starve")
              ? detector_set_starve_threshold(threshold) == 0
              : str_eq(argv[1], "jitter") &&
                detector_set_jitter_threshold(threshold) == 0)) {
            shell_print_error("detector", "uso: detector [starve <100..60000 ms>|jitter <1..1000 us>]", NULL);
            return 1;
        }
    }
    detector_stats_t stats;
    detector_stats(&stats);
    sched_jitter_t jitter;
    sched_jitter_snapshot(&jitter);
    kprintf("detector: chequeos=%u alertas=%u ultimo=%u ciclos (%u us) "
            "maximo=%u ciclos (%u us) grafo_max=%u ciclos "
            "dfs_max=%u ciclos frag_chequeos=%u frag_alertas=%u "
            "frag=%u%% libre=%u mayor=%u "
            "inanicion_chequeos=%u inanicion_alertas=%u "
            "espera_max=%u ms umbral=%u ms "
            "inversion_chequeos=%u inversion_alertas=%u "
            "inversion_max=%u ms "
            "jitter_chequeos=%u jitter_alertas=%u jitter_umbral=%u us\r\n",
            stats.runs, stats.alerts, stats.last_cycles,
            cycles_to_us(stats.last_cycles), stats.max_cycles,
            cycles_to_us(stats.max_cycles), stats.max_graph_cycles,
            stats.max_dfs_cycles, stats.frag_checks, stats.frag_alerts,
            stats.frag_percent, stats.frag_free, stats.frag_largest,
            stats.starve_checks, stats.starve_alerts,
            stats.starve_max_wait_ms, stats.starve_threshold_ms,
            stats.inversion_checks, stats.inversion_alerts,
            stats.inversion_max_ms, stats.jitter_checks,
            stats.jitter_alerts, stats.jitter_threshold_us);
    kprintf("jitter: muestras=%u min=%u us max=%u us media=%u us "
            "desviacion=%u us\r\n", jitter.samples, jitter.min_us,
            jitter.max_us, jitter.mean_us, jitter.stddev_us);
    return 0;
}

static int cmd_alerts(int argc, char **argv) {
    if (argc == 2 && str_eq(argv[1], "-c")) {
        alerts_clear();
        kprintf("alerts: registro limpio\r\n");
        return 0;
    }
    if (argc != 1) {
        shell_print_error("alerts", "uso: alerts [-c]", NULL);
        return 1;
    }
    uint32_t count = alerts_count();
    kprintf("alerts: %u recientes\r\n", count);
    for (uint32_t i = 0u; i < count; i++) {
        alert_t alert;
        if (!alerts_get(i, &alert)) {
            break;
        }
        kprintf("#%u t=%u ms %s %s: %s datos=%u,%u,%u\r\n",
                alert.sequence, alert.timestamp_ms,
                alerts_severity_name(alert.severity),
                alerts_type_name(alert.type), alert.message,
                alert.data[0], alert.data[1], alert.data[2]);
    }
    return 0;
}

static int cmd_health(int argc, char **argv) {
    (void)argv;
    if (argc != 1) {
        shell_print_error("health", "uso: health", NULL);
        return 1;
    }
    detector_stats_t stats;
    detector_stats(&stats);
    graph_t graph;
    graph_cycle_t cycle;
    (void)graph_build(&graph);
    int has_cycle = graph_find_cycle(&graph, &cycle);
    kprintf("health: deadlocks %s; ciclo=%u tareas\r\n",
            has_cycle ? "CRIT" : "OK",
            has_cycle ? (uint32_t)cycle.count : 0u);

    heap_stats_t heap;
    heap_stats(&heap);
    uint32_t frag = heap_frag_percent(&heap);
    kprintf("health: fragmentacion %s; frag=%u%% libre=%u B mayor=%u B\r\n",
            frag > 60u ? "CRIT" : frag > 30u ? "WARN" : "OK",
            frag, heap.free, heap.largest_free_block);

    uint32_t now = uptime_ms();
    uint32_t max_ready = 0u;
    uint32_t ps = irq_save();
    for (uint32_t tid = 1u; tid < MAX_TASKS; tid++) {
        tcb_t *task = task_get(tid);
        if (task != NULL && task->state == TASK_READY) {
            uint32_t wait = now - task->ready_since;
            if (wait > max_ready) {
                max_ready = wait;
            }
        }
    }
    irq_restore(ps);
    kprintf("health: inanicion %s; espera_actual=%u ms umbral=%u ms\r\n",
            max_ready > stats.starve_threshold_ms ? "WARN" : "OK",
            max_ready, stats.starve_threshold_ms);
    kprintf("health: inversion de prioridad %s; alertas_historicas=%u\r\n",
            stats.inversion_active ? "WARN" : "OK",
            stats.inversion_alerts);

    sched_jitter_t jitter;
    sched_jitter_snapshot(&jitter);
    if (jitter.samples == 0u) {
        kprintf("health: jitter de switch SIN DATOS (TRACE desactivado)\r\n");
    } else {
        kprintf("health: jitter de switch %s; muestras=%u min=%u us max=%u us "
                "media=%u us desviacion=%u us umbral=%u us\r\n",
                jitter.stddev_us > stats.jitter_threshold_us ? "ALERTA" : "OK",
                jitter.samples, jitter.min_us, jitter.max_us,
                jitter.mean_us, jitter.stddev_us, stats.jitter_threshold_us);
    }

    uint32_t lost_events = 0u;
    uint32_t lost_frames = 0u;
#if SENTINEL_TRACE
    lost_events = trace_dropped();
#endif
#if SENTINEL_TRANSPORT
    lost_events += transport_events_dropped();
    lost_frames = transport_frames_dropped();
#endif
    kprintf("health: eventos perdidos %s; eventos=%u tramas=%u\r\n",
            lost_events != 0u || lost_frames != 0u ? "WARN" : "OK",
            lost_events, lost_frames);
    uint32_t free_percent = heap.total != 0u
                            ? heap.free * 100u / heap.total : 0u;
    kprintf("health: heap libre %s; libre=%u B (%u%%)\r\n",
            free_percent < 10u ? "CRIT" :
            free_percent < 20u ? "WARN" : "OK",
            heap.free, free_percent);
    return 0;
}

static int cmd_help(int argc, char **argv) {
    if (argc >= 2) {
        for (uint32_t i = 0; i < shell_cmd_count(); i++) {
            if (str_eq(shell_cmd_name(i), argv[1])) {
                kprintf("%s - %s\r\nuso: %s\r\n", shell_cmd_name(i), shell_cmd_brief(i), shell_cmd_usage(i));
                return 0;
            }
        }
        shell_print_error("help", "no existe el comando", argv[1]);
        return 1;
    }
    kprintf("Comandos disponibles:\r\n");
    for (uint32_t i = 0; i < shell_cmd_count(); i++) {
        const char *name = shell_cmd_name(i);
        kprintf("  %s", name);
        for (uint32_t k = str_len(name); k < 10u; k++) {
            uart_putc(' ');
        }
        kprintf("%s\r\n", shell_cmd_brief(i));
    }
    return 0;
}

static int cmd_history(int argc, char **argv) {
    (void)argc;
    (void)argv;
    for (uint32_t i = 0; i < shell_history_count(); i++) {
        kprintf("%3u  %s\r\n", i + 1u, shell_history_get(i));
    }
    return 0;
}

static int cmd_reboot(int argc, char **argv) {
    (void)argc;
    (void)argv;
    kprintf("reiniciando...\r\n");
    uart_flush();
    (void)irq_disable();
    /* RTC_CNTL_OPTIONS0_REG (0x3FF48000), bit SW_SYS_RST (31): reinicio de sistema por software. */
    *(volatile uint32_t *)0x3FF48000u = 0x80000000u;
    for (;;) {
        __asm__ __volatile__("waiti 0");   /* espera el reinicio (no vuelve) */
    }
    return 0;
}

/* ---- ps ---- */

static uint64_t g_prev_cycles[MAX_TASKS];
static uint32_t g_prev_instance[MAX_TASKS];
static uint8_t  g_prev_valid[MAX_TASKS];

static int cmd_ps(int argc, char **argv) {
    int verbose = (argc >= 2 && str_eq(argv[1], "-v"));
    const char *name = NULL;
    if (argc > 3 || (argc == 3 && !verbose)) {
        shell_print_error("ps", "uso: ps [-v] [nombre]", NULL);
        return 1;
    }
    if (argc == 3) {
        name = argv[2];
    } else if (argc == 2 && !verbose) {
        name = argv[1];
    }

    /* % de CPU sobre la ventana desde el ultimo `ps` (la primera vez, desde el arranque). */
    /* cpu_cycles es de 64 bits y el tick lo actualiza: en un nucleo de 32 bits una lectura
     * normal podria partirse justo en el acarreo. Se toma una foto de todos bajo candado. */
    uint64_t cyc[MAX_TASKS];
    uint32_t instance[MAX_TASKS];
    uint32_t ps = irq_save();
    for (uint32_t i = 0; i < MAX_TASKS; i++) {
        tcb_t *t = task_get(i);
        cyc[i] = (t != NULL) ? t->cpu_cycles : 0u;
        instance[i] = (t != NULL) ? t->instance_id : 0u;
    }
    irq_restore(ps);

    uint64_t delta[MAX_TASKS];
    uint64_t total = 0;
    for (uint32_t i = 0; i < MAX_TASKS; i++) {
        tcb_t *t = task_get(i);
        delta[i] = 0;
        if (t != NULL) {
            /* El TID puede reciclarse entre dos invocaciones de ps. Su
             * contador CPU empieza de cero: restar el de la tarea anterior
             * produciria underflow y porcentajes mayores que 100. */
            delta[i] = (g_prev_valid[i] && g_prev_instance[i] == instance[i] &&
                        cyc[i] >= g_prev_cycles[i]) ?
                       (cyc[i] - g_prev_cycles[i]) : cyc[i];
            total += delta[i];
        }
    }
    uint32_t shift = 0;
    while ((total >> shift) > 0x3FFFFFu) {
        shift++;
    }
    uint32_t total32 = (uint32_t)(total >> shift);

    if (verbose) {
        kprintf("TID NOMBRE     ESTADO    PRI  CPU%%    MEM  CAMBIOS  CPUms  ESPERAms  PILA  BLOQms\r\n");
    } else {
        kprintf("TID NOMBRE     ESTADO    PRI  CPU%%    MEM\r\n");
    }
    for (uint32_t i = 0; i < MAX_TASKS; i++) {
        tcb_t *t = task_get(i);
        if (t == NULL || (name != NULL && !str_eq(t->name, name))) {
            continue;
        }
        uint32_t pct = (total32 > 0u) ? (((uint32_t)(delta[i] >> shift)) * 100u) / total32 : 0u;
        uint32_t mem = 0;
        uint32_t blocks = 0;
        heap_usage_by_task(t->tid, &mem, &blocks);

        const char *st = task_state_name(t->state);
        kprintf("%3u ", (uint32_t)t->tid);
        kprintf("%s", t->name);
        for (uint32_t k = str_len(t->name); k < 10u; k++) {
            uart_putc(' ');
        }
        kprintf("%s", st);
        for (uint32_t k = str_len(st); k < 10u; k++) {
            uart_putc(' ');
        }
        kprintf("%3u %4u%% %6u", (uint32_t)t->priority, pct, mem);
        if (verbose) {
            uint32_t blocked_ms = (t->state == TASK_BLOCKED) ?
                                  uptime_ms() - t->wait_started_ms : 0u;
            kprintf("  %7u %6u %9u  %u/%u  %u", t->switches_in, cycles_to_ms64(cyc[i]),
                    t->max_ready_wait, task_stack_used(t), t->stack_size, blocked_ms);
        }
        kprintf("\r\n");
    }
    for (uint32_t i = 0; i < MAX_TASKS; i++) {
        tcb_t *t = task_get(i);
        g_prev_valid[i] = (t != NULL);
        g_prev_cycles[i] = cyc[i];
        g_prev_instance[i] = instance[i];
    }
    return 0;
}

/* ---- run / kill: tareas de demostracion integradas ---- */

static volatile uint32_t g_counter_value;

static void prog_blink(void *arg) {
    uint32_t period = (uint32_t)(uintptr_t)arg;
    gpio_set_output(2);
    for (;;) {
        gpio_set_high(2);
        task_sleep(period);
        gpio_set_low(2);
        task_sleep(period);
    }
}

static void prog_counter(void *arg) {
    (void)arg;
    for (;;) {
        g_counter_value++;
        task_sleep(100);
    }
}

static void prog_hog(void *arg) {
    (void)arg;
    for (;;) {
        __asm__ __volatile__("");
    }
}

static void prog_sleeper(void *arg) {
    (void)arg;
    for (;;) {
        task_sleep(1000);
    }
}

static void prog_alloc(void *arg) {
    (void)arg;
    kmalloc(256);
    kmalloc(256);
    kmalloc(256);
    kmalloc(256);
    for (;;) {
        task_sleep(1000);
    }
}

typedef struct {
    const char *name;
    task_fn_t   fn;
    uint8_t     priority;
    const char *what;
} prog_t;

static const prog_t k_progs[] = {
    {"blink",   prog_blink,   5, "parpadea el LED (GPIO2); arg: periodo en ms (500)"},
    {"counter", prog_counter, 5, "incrementa un contador cada 100 ms"},
    {"hog",     prog_hog,     6, "bucle infinito: consume toda la CPU que le dejen"},
    {"sleeper", prog_sleeper, 5, "duerme 1 s en bucle (no consume CPU)"},
    {"alloc",   prog_alloc,   5, "reserva 4 bloques de 256 B y duerme (se ve en MEM)"},
};
#define N_PROGS (sizeof(k_progs) / sizeof(k_progs[0]))

static int cmd_run(int argc, char **argv) {
    if (argc < 2) {
        kprintf("Programas:\r\n");
        for (uint32_t i = 0; i < N_PROGS; i++) {
            kprintf("  %s", k_progs[i].name);
            for (uint32_t k = str_len(k_progs[i].name); k < 9u; k++) {
                uart_putc(' ');
            }
            kprintf("%s\r\n", k_progs[i].what);
        }
        return 0;
    }
    for (uint32_t i = 0; i < N_PROGS; i++) {
        if (str_eq(k_progs[i].name, argv[1])) {
            uint32_t arg = 500;
            if (argc >= 3 && shell_parse_uint(argv[2], &arg) != 0) {
                shell_print_error("run", "el argumento debe ser un numero", argv[2]);
                return 1;
            }
            int32_t tid = task_create(k_progs[i].name, k_progs[i].fn, (void *)(uintptr_t)arg,
                                      k_progs[i].priority, 0);
            if (tid < 0) {
                shell_print_error("run", "no se pudo crear la tarea (tabla llena)", NULL);
                return 1;
            }
            kprintf("run: tarea %d (%s) creada\r\n", tid, k_progs[i].name);
            return 0;
        }
    }
    shell_print_error("run", "programa desconocido (run sin argumentos los lista)", argv[1]);
    return 1;
}

static int cmd_kill(int argc, char **argv) {
    uint32_t tid;
    if (argc < 2 || shell_parse_uint(argv[1], &tid) != 0) {
        shell_print_error("kill", "uso: kill <tid>", NULL);
        return 1;
    }
    if (tid == 0u) {
        shell_print_error("kill", "no se puede matar a idle (tid 0)", NULL);
        return 1;
    }
    if (tid == shell_tid()) {
        shell_print_error("kill", "no se puede matar al propio shell", NULL);
        return 1;
    }
    tcb_t *t = task_get(tid);
    if (t == NULL) {
        kprintf("kill: no existe la tarea %u\r\n", tid);
        return 1;
    }
    char name[TASK_NAME_LEN];
    uint32_t i = 0;
    for (; i + 1u < TASK_NAME_LEN && t->name[i] != '\0'; i++) {
        name[i] = t->name[i];
    }
    name[i] = '\0';
    if (task_kill(tid) != 0) {
        shell_print_error("kill", "no se pudo terminar la tarea", NULL);
        return 1;
    }
    kprintf("kill: tarea %u (%s) terminada\r\n", tid, name);
    return 0;
}

/* ---- registro ---- */

void shell_register_builtins(void) {
    inject_register_command();
#if SENTINEL_TRACE
    trace_register_command();
#endif
    cmd_register("help",    cmd_help,    "lista los comandos", "help [comando]");
    cmd_register("ls",      cmd_ls,      "lista un directorio", "ls [ruta]");
    cmd_register("cd",      cmd_cd,      "cambia de directorio", "cd [ruta]   (sin argumentos: /home/iker)");
    cmd_register("pwd",     cmd_pwd,     "muestra el directorio actual", "pwd");
    cmd_register("cat",     cmd_cat,     "muestra un archivo", "cat <archivo>");
    cmd_register("mkdir",   cmd_mkdir,   "crea un directorio", "mkdir <ruta>");
    cmd_register("touch",   cmd_touch,   "crea un archivo vacio", "touch <archivo>");
    cmd_register("write",   cmd_write,   "escribe texto en un archivo", "write <archivo> <texto...>");
    cmd_register("rm",      cmd_rm,      "borra archivos o directorios", "rm [-r] <ruta>");
    cmd_register("echo",    cmd_echo,    "imprime su texto", "echo <texto...>");
    cmd_register("ps",      cmd_ps,      "lista las tareas", "ps [-v] [nombre]");
    cmd_register("free",    cmd_free,    "estado del heap y fragmentacion", "free [-v]   (-v: mapa del heap)");
    cmd_register("sync",    cmd_sync,    "muestra propietarios y esperas de mutex", "sync");
    cmd_register("graph",   cmd_graph,   "muestra aristas del grafo de espera", "graph");
    cmd_register("deadlock", cmd_deadlock, "comprueba un ciclo de espera ahora", "deadlock");
    cmd_register("detector", cmd_detector, "muestra el coste y las alertas del detector", "detector [starve <100..60000 ms>|jitter <1..1000 us>]");
    cmd_register("alerts", cmd_alerts, "lista o limpia las ultimas 32 alertas", "alerts [-c]");
    cmd_register("health", cmd_health, "resume la salud de cada categoria", "health");
    cmd_register("uptime",  cmd_uptime,  "tiempo desde el arranque", "uptime");
    cmd_register("uname",   cmd_uname,   "informacion del sistema", "uname [-a]");
    cmd_register("clear",   cmd_clear,   "limpia la pantalla", "clear");
    cmd_register("history", cmd_history, "ultimos comandos", "history");
    cmd_register("run",     cmd_run,     "lanza una tarea de demostracion", "run [programa] [argumento]");
    cmd_register("kill",    cmd_kill,    "termina una tarea", "kill <tid>");
    cmd_register("reboot",  cmd_reboot,  "reinicia el sistema", "reboot");
}
