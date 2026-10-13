/* Pruebas en placa de E05 (VFS). La logica fina de rutas y nodos ya se probo en el PC
 * (make test: tests/test_vfs.c); aqui se comprueba lo que depende del kernel: el arbol de
 * demostracion, /proc, el cwd y los descriptores por tarea, y la limpieza al terminar. */

#include "selftest.h"
#include <sentinel/vfs.h>
#include <sentinel/procfs.h>
#include <sentinel/heap.h>
#include <sentinel/task.h>
#include <sentinel/kprintf.h>
#include "sched.h"
#include "time.h"

static int contains(const char *text, const char *needle) {
    for (const char *p = text; *p != '\0'; p++) {
        const char *a = p;
        const char *k = needle;
        while (*k != '\0' && *a == *k) {
            a++;
            k++;
        }
        if (*k == '\0') {
            return 1;
        }
    }
    return 0;
}

/* Numero que sigue a 'key' en 'text' (0xFFFFFFFF si no esta). */
static uint32_t field(const char *text, const char *key) {
    for (const char *p = text; *p != '\0'; p++) {
        const char *a = p;
        const char *k = key;
        while (*k != '\0' && *a == *k) {
            a++;
            k++;
        }
        if (*k == '\0') {
            while (*a == ' ') {
                a++;
            }
            uint32_t v = 0;
            while (*a >= '0' && *a <= '9') {
                v = v * 10u + (uint32_t)(*a - '0');
                a++;
            }
            return v;
        }
    }
    return 0xFFFFFFFFu;
}

static int slurp(const char *path, char *buf, uint32_t cap) {
    int fd = vfs_open(path, VFS_O_RDONLY, NULL);
    if (fd < 0) {
        return fd;
    }
    int n = vfs_read_fd(fd, buf, cap - 1u);
    vfs_close(fd);
    if (n >= 0) {
        buf[n] = '\0';
    }
    return n;
}

/* ---- HU-E05-06: arbol de demostracion ---- */

static void t_demo(void) {
    int all = 1;
    const char *dirs[] = {"/bin", "/etc", "/home", "/home/iker", "/home/iker/notas", "/proc", "/tmp"};
    for (uint32_t i = 0; i < sizeof(dirs) / sizeof(dirs[0]); i++) {
        vnode_t *n = vfs_resolve(dirs[i], NULL);
        if (n == NULL || n->type != VN_DIR) {
            all = 0;
        }
    }
    check("el arbol de demostracion existe: /bin /etc /home/iker/notas /proc /tmp ...", all);

    char buf[128];
    check("cat /etc/motd funciona desde el arranque", slurp("/etc/motd", buf, sizeof(buf)) > 10 &&
          contains(buf, "Sentinel OS"));
    check("existen /etc/version y /home/iker/readme.txt",
          vfs_resolve("/etc/version", NULL) != NULL && vfs_resolve("/home/iker/readme.txt", NULL) != NULL);
    kprintf("la precarga del arbol usa %u B de heap\r\n", vfs_demo_heap_bytes());
    check("la precarga consume < 2 KB de heap", vfs_demo_heap_bytes() < 2048u);
}

/* ---- HU-E05-07: /proc ---- */

static void t_proc(void) {
    char buf[512];

    check("/proc/version contiene el nombre del sistema",
          slurp("/proc/version", buf, sizeof(buf)) > 0 && contains(buf, "Sentinel OS"));

    check("/proc/tasks lista a ctl y a idle",
          slurp("/proc/tasks", buf, sizeof(buf)) > 0 && contains(buf, "ctl") && contains(buf, "idle"));

    slurp("/proc/uptime", buf, sizeof(buf));
    uint32_t up1 = field(buf, "(");
    task_sleep(60);
    slurp("/proc/uptime", buf, sizeof(buf));
    uint32_t up2 = field(buf, "(");
    check("/proc/uptime se genera al leer: avanza ~60 ms entre dos lecturas",
          up2 != 0xFFFFFFFFu && up2 - up1 >= 55u && up2 - up1 <= 80u);

    /* Coherencia con el heap real: crear un archivo grande sube 'Usado'; borrarlo lo baja. */
    slurp("/proc/meminfo", buf, sizeof(buf));
    uint32_t used1 = field(buf, "Usado:");
    uint32_t total = field(buf, "Total:");
    vfs_create("/tmp/big", NULL);
    static char payload[2000];
    for (uint32_t i = 0; i < sizeof(payload); i++) {
        payload[i] = 'x';
    }
    vfs_write(vfs_resolve("/tmp/big", NULL), 0, payload, sizeof(payload));
    slurp("/proc/meminfo", buf, sizeof(buf));
    uint32_t used2 = field(buf, "Usado:");
    vfs_unlink("/tmp/big", NULL);
    slurp("/proc/meminfo", buf, sizeof(buf));
    uint32_t used3 = field(buf, "Usado:");
    kprintf("meminfo: usado %u -> %u (con archivo de 2000 B) -> %u\r\n", used1, used2, used3);
    check("/proc/meminfo: crear un archivo de 2000 B sube 'Usado' al menos 2000 B", used2 >= used1 + 2000u);
    check("/proc/meminfo: al borrarlo 'Usado' vuelve al valor inicial exacto", used3 == used1);
    check("/proc/meminfo: Total = 98304", total == HEAP_SIZE);

    char again[512];
    meminfo_text(again, sizeof(again));
    slurp("/proc/meminfo", buf, sizeof(buf));
    check("meminfo_text() (el que usa `free`) coincide exactamente con /proc/meminfo", contains(buf, "Fragmentacion") &&
          field(again, "Usado:") == field(buf, "Usado:"));
}

/* ---- HU-E05-05 y cwd por tarea ---- */

static volatile uint32_t g_cwd_state;     /* 1 = listo, 2 = termino */
static volatile int g_cwd_rel_ok;

static void cwd_task(void *arg) {
    (void)arg;
    int rc = vfs_chdir("/tmp/w/deep");
    char b[VFS_PATH_MAX];
    int got = vfs_getcwd(b, sizeof(b));
    g_cwd_rel_ok = (rc == 0 && got == 0 && b[0] == '/' && b[5] == 'w' &&
                    vfs_resolve("../marca", vfs_cwd()) != NULL &&
                    vfs_resolve("..", vfs_cwd()) == vfs_resolve("/tmp/w", NULL));
    g_cwd_state = 1;
    task_sleep(400);
    g_cwd_state = 2;
}

static void t_cwd(void) {
    vfs_mkdir("/tmp/w", NULL);
    vfs_mkdir("/tmp/w/deep", NULL);
    vfs_create("/tmp/w/marca", NULL);

    g_cwd_state = 0;
    task_create("cwdt", cwd_task, (void *)0, 2, 0);
    task_sleep(100);
    check("cada tarea tiene su cwd: la nueva hace cd y resuelve rutas relativas desde ahi",
          g_cwd_state == 1u && g_cwd_rel_ok);

    char b[VFS_PATH_MAX];
    vfs_getcwd(b, sizeof(b));
    check("el cwd de ctl no cambio (sigue siendo '/')", b[0] == '/' && b[1] == '\0');
    check("no se puede borrar el directorio que es cwd de otra tarea (EBUSY)",
          vfs_rm_recursive("/tmp/w", NULL) == VFS_EBUSY && vfs_rmdir("/tmp/w/deep", NULL) == VFS_EBUSY);

    task_sleep(450);
    check("al terminar la tarea se suelta su cwd y ya se puede borrar todo el arbol",
          g_cwd_state == 2u && vfs_rm_recursive("/tmp/w", NULL) == 0 && vfs_resolve("/tmp/w", NULL) == NULL);
}

static void fd_leaker(void *arg) {
    (void)arg;
    vfs_open("/tmp/abierto", VFS_O_WRONLY | VFS_O_CREAT, NULL);   /* y no lo cierra */
    vfs_open("/etc/motd", VFS_O_RDONLY, NULL);
}

static void cwd_sleeper(void *arg) {
    (void)arg;
    vfs_chdir("/tmp/k");
    for (;;) {
        task_sleep(100);
    }
}

static void t_cleanup(void) {
    task_create("fdleak", fd_leaker, (void *)0, 2, 0);
    task_sleep(80);
    vnode_t *f = vfs_resolve("/tmp/abierto", NULL);
    vnode_t *motd = vfs_resolve("/etc/motd", NULL);
    check("al terminar, la tarea que dejo descriptores abiertos los libera (refs = 0)",
          f != NULL && f->refs == 0u && motd != NULL && motd->refs == 0u);
    check("y el archivo se puede borrar", vfs_unlink("/tmp/abierto", NULL) == 0);

    vfs_mkdir("/tmp/k", NULL);
    vfs_create("/tmp/k/f", NULL);
    int32_t t = task_create("cwdk", cwd_sleeper, (void *)0, 2, 0);
    task_sleep(50);
    check("mientras la tarea vive, su directorio esta ocupado", vfs_rm_recursive("/tmp/k", NULL) == VFS_EBUSY);
    task_kill((uint32_t)t);
    check("tras `kill` se libera su cwd y el arbol se puede borrar", vfs_rm_recursive("/tmp/k", NULL) == 0);
}

/* ---- correccion de la revision: el arbol del VFS no pertenece a la tarea que lo creo ---- */

static void file_maker(void *arg) {
    (void)arg;
    vfs_create("/tmp/de_la_tarea", NULL);
    vfs_write(vfs_resolve("/tmp/de_la_tarea", NULL), 0, "sigo vivo", 9);
    for (;;) {
        task_sleep(100);
    }
}

static void t_kill_keeps_tree(void) {
    int32_t t = task_create("fmaker", file_maker, (void *)0, 2, 0);
    task_sleep(60);
    vnode_t *f = vfs_resolve("/tmp/de_la_tarea", NULL);
    check("la tarea creo un archivo en el VFS", f != NULL && f->size == 9u);
    task_kill((uint32_t)t);
    char buf[16];
    f = vfs_resolve("/tmp/de_la_tarea", NULL);
    int n = (f != NULL) ? vfs_read(f, 0, buf, sizeof(buf) - 1u) : -1;
    check("tras `kill` el archivo sigue intacto (no hay punteros colgantes)",
          f != NULL && n == 9 && buf[0] == 's' && buf[8] == 'o');
    vfs_unlink("/tmp/de_la_tarea", NULL);
}

/* ---- F2: fugas ---- */

static void t_leaks(void) {
    heap_stats_t before;
    heap_stats_t after;
    heap_stats(&before);

    char path[32];
    for (uint32_t i = 0; i < 40u; i++) {
        path[0] = '/'; path[1] = 't'; path[2] = 'm'; path[3] = 'p'; path[4] = '/'; path[5] = 'n';
        path[6] = (char)('0' + (i / 10u)); path[7] = (char)('0' + (i % 10u)); path[8] = '\0';
        if ((i % 2u) == 0u) {
            vfs_mkdir(path, NULL);
        } else {
            vfs_create(path, NULL);
            vfs_write(vfs_resolve(path, NULL), 0, "contenido de prueba", 19);
        }
    }
    int ok = 1;
    for (uint32_t i = 0; i < 40u; i++) {
        path[0] = '/'; path[1] = 't'; path[2] = 'm'; path[3] = 'p'; path[4] = '/'; path[5] = 'n';
        path[6] = (char)('0' + (i / 10u)); path[7] = (char)('0' + (i % 10u)); path[8] = '\0';
        int rc = ((i % 2u) == 0u) ? vfs_rmdir(path, NULL) : vfs_unlink(path, NULL);
        if (rc != 0) {
            ok = 0;
        }
    }
    heap_stats(&after);
    check("crear y borrar 40 nodos en la placa deja el heap en su estado exacto",
          ok && after.used == before.used && after.block_count == before.block_count);
}

void test_e05(void) {
    t_demo();
    t_proc();
    t_cwd();
    t_cleanup();
    t_kill_keeps_tree();
    t_leaks();
    kprintf("fin de pruebas E05\r\n");
}
