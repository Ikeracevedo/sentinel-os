/* /proc: archivos virtuales cuyo contenido se GENERA en el momento de leerlos (no se
 * almacena nada). Es el principio Unix de "todo es un archivo": cat /proc/meminfo y el
 * comando free usan el mismo generador, por eso muestran lo mismo.
 *
 * Tambien el arbol de demostracion que se precarga al arrancar. */

#include <sentinel/procfs.h>
#include <sentinel/vfs.h>
#include <sentinel/heap.h>
#include <sentinel/task.h>
#include "../core/time.h"
#include "../arch/xtensa/cpu.h"

/* ---- constructor de cadenas (no hay snprintf) ---- */

typedef struct {
    char    *b;
    uint32_t cap;
    uint32_t len;
} sb_t;

static void sb_init(sb_t *s, char *buf, uint32_t cap) {
    s->b = buf;
    s->cap = cap;
    s->len = 0;
    if (cap > 0u) {
        buf[0] = '\0';
    }
}

static void sb_char(sb_t *s, char c) {
    if (s->len + 1u < s->cap) {
        s->b[s->len++] = c;
        s->b[s->len] = '\0';
    }
}

static void sb_str(sb_t *s, const char *str) {
    while (*str != '\0') {
        sb_char(s, *str++);
    }
}

static void sb_uint(sb_t *s, uint32_t v) {
    char tmp[11];
    uint32_t n = 0;
    if (v == 0u) {
        tmp[n++] = '0';
    }
    while (v > 0u) {
        tmp[n++] = (char)('0' + (v % 10u));
        v /= 10u;
    }
    while (n > 0u) {
        sb_char(s, tmp[--n]);
    }
}

/* Rellena con espacios hasta que el campo que empezo en 'start' mida 'width'. */
static void sb_pad(sb_t *s, uint32_t start, uint32_t width) {
    while (s->len - start < width && s->len + 1u < s->cap) {
        sb_char(s, ' ');
    }
}

/* ---- generadores ---- */

uint32_t meminfo_text(char *buf, uint32_t cap) {
    heap_stats_t h;
    heap_stats(&h);
    sb_t s;
    sb_init(&s, buf, cap);

    sb_str(&s, "Total:       "); sb_uint(&s, h.total); sb_str(&s, " B\n");
    sb_str(&s, "Usado:       "); sb_uint(&s, h.used); sb_str(&s, " B (");
    sb_uint(&s, (h.used * 100u) / h.total); sb_str(&s, "%)\n");
    sb_str(&s, "Libre:       "); sb_uint(&s, h.free); sb_str(&s, " B\n");
    sb_str(&s, "MayorLibre:  "); sb_uint(&s, h.largest_free_block); sb_str(&s, " B\n");
    sb_str(&s, "Fragmentacion: "); sb_uint(&s, heap_frag_percent(&h)); sb_str(&s, "%\n");
    sb_str(&s, "Pico:        "); sb_uint(&s, h.peak_used); sb_str(&s, " B\n");
    sb_str(&s, "Bloques:     "); sb_uint(&s, h.block_count); sb_str(&s, " (");
    sb_uint(&s, h.block_count - h.free_block_count); sb_str(&s, " usados, ");
    sb_uint(&s, h.free_block_count); sb_str(&s, " libres)\n");
    return s.len;
}

const char *task_state_name(task_state_t st) {
    switch (st) {
    case TASK_READY:      return "READY";
    case TASK_RUNNING:    return "RUNNING";
    case TASK_BLOCKED:    return "BLOCKED";
    case TASK_SLEEPING:   return "SLEEPING";
    case TASK_SUSPENDED:  return "SUSPENDED";
    case TASK_TERMINATED: return "TERMINATED";
    default:              return "UNUSED";
    }
}

uint32_t tasks_text(char *buf, uint32_t cap) {
    sb_t s;
    sb_init(&s, buf, cap);
    sb_str(&s, "TID NOMBRE     ESTADO    PRI\n");
    for (uint32_t i = 0; i < MAX_TASKS; i++) {
        tcb_t *t = task_get(i);
        if (t == NULL) {
            continue;
        }
        uint32_t c = s.len;
        sb_uint(&s, t->tid);
        sb_pad(&s, c, 4);
        c = s.len;
        sb_str(&s, t->name);
        sb_pad(&s, c, 11);
        c = s.len;
        sb_str(&s, task_state_name(t->state));
        sb_pad(&s, c, 10);
        sb_uint(&s, t->priority);
        sb_char(&s, '\n');
    }
    return s.len;
}

uint32_t uptime_text(char *buf, uint32_t cap) {
    uint32_t ms = uptime_ms();
    uint32_t sec = ms / 1000u;
    sb_t s;
    sb_init(&s, buf, cap);
    sb_str(&s, "up ");
    sb_uint(&s, sec / 86400u); sb_str(&s, "d ");
    sb_uint(&s, (sec / 3600u) % 24u); sb_str(&s, "h ");
    sb_uint(&s, (sec / 60u) % 60u); sb_str(&s, "m ");
    sb_uint(&s, sec % 60u); sb_str(&s, "s (");
    sb_uint(&s, ms); sb_str(&s, " ms)\n");
    return s.len;
}

uint32_t version_text(char *buf, uint32_t cap) {
    sb_t s;
    sb_init(&s, buf, cap);
    sb_str(&s, "Sentinel OS v0.1 (xtensa-esp32, ");
    sb_uint(&s, CPU_FREQ_HZ / 1000000u);
    sb_str(&s, " MHz) build " __DATE__ " " __TIME__ "\n");
    return s.len;
}

void procfs_init(void) {
    vfs_mkdir("/proc", NULL);
    vfs_create_virtual("/proc/meminfo", NULL, meminfo_text);
    vfs_create_virtual("/proc/tasks", NULL, tasks_text);
    vfs_create_virtual("/proc/uptime", NULL, uptime_text);
    vfs_create_virtual("/proc/version", NULL, version_text);
}

/* ---- arbol de demostracion ---- */

static uint32_t g_demo_bytes;

static void put_file(const char *path, const char *text) {
    uint32_t n = 0;
    while (text[n] != '\0') {
        n++;
    }
    if (vfs_create(path, NULL) == VFS_OK) {
        vfs_write(vfs_resolve(path, NULL), 0, text, n);
    }
}

uint32_t vfs_demo_heap_bytes(void) {
    return g_demo_bytes;
}

void vfs_populate_demo(void) {
    heap_stats_t before;
    heap_stats_t after;
    heap_stats(&before);

    vfs_mkdir("/bin", NULL);
    vfs_mkdir("/etc", NULL);
    vfs_mkdir("/home", NULL);
    vfs_mkdir("/home/iker", NULL);
    vfs_mkdir("/home/iker/notas", NULL);
    vfs_mkdir("/tmp", NULL);
    put_file("/etc/motd", "Sentinel OS v0.1 - built " __DATE__ "\n");
    put_file("/etc/version", "0.1\n");
    put_file("/home/iker/readme.txt", "Bienvenido a Sentinel OS. Escribe help.\n");

    heap_stats(&after);
    g_demo_bytes = after.used - before.used;
}
