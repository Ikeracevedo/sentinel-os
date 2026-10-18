/* Shell: un REPL sobre UART que corre COMO UNA TAREA MAS del scheduler.
 *
 * Entrada: el tick (ISR, 1 kHz) lee el FIFO de recepcion del UART, mete los bytes en un
 * ring buffer y hace sem_post por cada uno. La tarea del shell hace sem_wait: mientras no
 * hay nada que leer queda BLOCKED y NO consume CPU (un busy-wait falsearia `ps`).
 *
 * Linea: edicion basica (backspace, Ctrl+C), flechas arriba/abajo para el historial
 * (maquina de estados para las secuencias ESC [ A / ESC [ B) y limite de 128 caracteres. */

#include <sentinel/shell.h>
#include <sentinel/sync.h>
#include <sentinel/task.h>
#include <sentinel/vfs.h>
#include <sentinel/kprintf.h>
#include "../drivers/uart.h"
#include "../core/sched.h"

#ifndef SENTINEL_TRANSPORT
#define SENTINEL_TRANSPORT 0
#endif
#if SENTINEL_TRANSPORT
#include <sentinel/downlink.h>
#include <sentinel/frame.h>
#include <sentinel/trace.h>
#endif

#if SENTINEL_TRANSPORT
#define RX_RING 512u
#else
#define RX_RING 128u
#endif
#define MAX_CMDS 40u

typedef struct {
    const char *name;
    cmd_fn      fn;
    const char *brief;
    const char *usage;
} cmd_t;

static cmd_t    g_cmds[MAX_CMDS];
static uint32_t g_ncmds;

static uint32_t g_shell_tid;
static int      g_last_status;

/* ---- tabla de comandos ---- */

int cmd_register(const char *name, cmd_fn fn, const char *brief, const char *usage) {
    if (g_ncmds >= MAX_CMDS || name == NULL || fn == NULL) {
        return -1;
    }
    g_cmds[g_ncmds].name = name;
    g_cmds[g_ncmds].fn = fn;
    g_cmds[g_ncmds].brief = brief;
    g_cmds[g_ncmds].usage = usage;
    g_ncmds++;
    return 0;
}

uint32_t shell_cmd_count(void) { return g_ncmds; }
const char *shell_cmd_name(uint32_t i) { return g_cmds[i].name; }
const char *shell_cmd_brief(uint32_t i) { return g_cmds[i].brief; }
const char *shell_cmd_usage(uint32_t i) { return g_cmds[i].usage; }
uint32_t shell_tid(void) { return g_shell_tid; }
int shell_last_status(void) { return g_last_status; }

static int str_eq(const char *a, const char *b) {
    while (*a != '\0' && *a == *b) {
        a++;
        b++;
    }
    return *a == *b;
}

int shell_parse_uint(const char *s, uint32_t *out) {
    if (s == NULL || *s == '\0') {
        return -1;
    }
    uint32_t v = 0;
    for (; *s != '\0'; s++) {
        if (*s < '0' || *s > '9') {
            return -1;
        }
        v = v * 10u + (uint32_t)(*s - '0');
    }
    *out = v;
    return 0;
}

/* Mensajes de error legibles: "cd: no existe el directorio: /hoem" (nunca "-2"). */
void shell_print_error(const char *cmd, const char *what, const char *arg) {
    if (arg != NULL) {
        kprintf("%s: %s: %s\r\n", cmd, what, arg);
    } else {
        kprintf("%s: %s\r\n", cmd, what);
    }
}

/* ---- entrada desde el UART ---- */

static uint8_t           g_rx[RX_RING];
static volatile uint32_t g_rx_head;
static uint32_t          g_rx_tail;
static sem_t             g_rx_sem;
#if SENTINEL_TRANSPORT
static downlink_parser_t g_downlink;
static uint8_t g_command[SHELL_LINE_MAX];
static uint16_t g_command_len;
static uint16_t g_command_pos;

static int apply_trace_cfg(const downlink_frame_t *frame) {
    const uint8_t *p = frame->payload;
    if (frame->length == 2u && p[0] == FRAME_TRACE_ENABLE && p[1] <= 1u) {
        trace_set_enabled(p[1]);
        return 1;
    }
    if (frame->length == 3u && p[0] == FRAME_TRACE_FILTER && p[2] <= 1u) {
        return trace_set_filter(p[1], p[2]) == 0;
    }
    if (frame->length == 3u && p[0] == FRAME_TRACE_SAMPLE) {
        uint32_t rate = (uint32_t)p[1] | ((uint32_t)p[2] << 8);
        return trace_set_sample_rate(rate) == 0;
    }
    return 0;
}
#endif

/* Corre dentro del ISR del tick. Productor unico (head); la tarea es el consumidor (tail). */
static void rx_poll_hook(void) {
    int c;
    while ((c = uart_rx_byte()) >= 0) {
        uint32_t next = (g_rx_head + 1u) % RX_RING;
        if (next == g_rx_tail) {
            continue;                       /* ring lleno: se descarta (nadie lo lee a tiempo) */
        }
        g_rx[g_rx_head] = (uint8_t)c;
        g_rx_head = next;
        sem_post(&g_rx_sem);
    }
}

static int shell_getc(void) {
    for (;;) {
#if SENTINEL_TRANSPORT
        if (g_command_pos < g_command_len) {
            return g_command[g_command_pos++];
        }
#endif
        sem_wait(&g_rx_sem);                /* BLOCKED hasta que llegue un byte */
        int c = g_rx[g_rx_tail];
        g_rx_tail = (g_rx_tail + 1u) % RX_RING;
#if SENTINEL_TRANSPORT
        downlink_frame_t frame;
        uint32_t crc_errors_before = g_downlink.crc_errors;
        if (!downlink_feed(&g_downlink, (uint8_t)c, &frame)) {
            if (g_downlink.crc_errors != crc_errors_before) {
                kprintf("downlink: CRC invalido (total=%u)\r\n",
                        g_downlink.crc_errors);
            }
            continue;
        }
        if (frame.kind == FRAME_TRACE_CFG) {
            kprintf("trace cfg: %s\r\n", apply_trace_cfg(&frame) ? "ok" : "invalida");
            continue;
        }
        if (!downlink_command_valid(frame.payload, frame.length)) {
            continue;
        }
        for (uint16_t i = 0u; i < frame.length; i++) {
            g_command[i] = frame.payload[i];
        }
        g_command[frame.length] = '\r';
        g_command_pos = 0u;
        g_command_len = (uint16_t)(frame.length + 1u);
#else
        return c;
#endif
    }
}

/* ---- historial ---- */

static char     g_hist[SHELL_HISTORY][SHELL_LINE_MAX];
static uint32_t g_hist_n;                   /* cuantas hay (<= SHELL_HISTORY) */
static uint32_t g_hist_next;                /* donde se guarda la proxima */

static void copy_str(char *dst, const char *src, uint32_t cap) {
    uint32_t i = 0;
    for (; i + 1u < cap && src[i] != '\0'; i++) {
        dst[i] = src[i];
    }
    dst[i] = '\0';
}

uint32_t shell_history_count(void) { return g_hist_n; }

const char *shell_history_get(uint32_t i) {
    uint32_t oldest = (g_hist_n < SHELL_HISTORY) ? 0u : g_hist_next;
    return g_hist[(oldest + i) % SHELL_HISTORY];
}

static void history_add(const char *line) {
    if (line[0] == '\0') {
        return;
    }
    if (g_hist_n > 0u && str_eq(shell_history_get(g_hist_n - 1u), line)) {
        return;                             /* no repetir consecutivos */
    }
    copy_str(g_hist[g_hist_next], line, SHELL_LINE_MAX);
    g_hist_next = (g_hist_next + 1u) % SHELL_HISTORY;
    if (g_hist_n < SHELL_HISTORY) {
        g_hist_n++;
    }
}

/* back = 1 es el comando anterior, 2 el anterior a ese... */
static const char *history_back(uint32_t back) {
    return shell_history_get(g_hist_n - back);
}

/* ---- linea ---- */

static void print_prompt(void) {
    char cwd[VFS_PATH_MAX];
    if (vfs_getcwd(cwd, sizeof(cwd)) != VFS_OK) {
        cwd[0] = '?';
        cwd[1] = '\0';
    }
    kprintf("sentinel:%s$ ", cwd);
}

/* Redibuja la linea completa (tras recuperar del historial). */
static void redraw(const char *line) {
    kprintf("\r\033[K");
    print_prompt();
    kprintf("%s", line);
}

/* Lee una linea. Devuelve su longitud (0 si estaba vacia o se descarto con Ctrl+C). */
static uint32_t read_line(char *buf) {
    static int last_was_cr;
    uint32_t len = 0;
    int esc = 0;                            /* 0 normal, 1 tras ESC, 2 dentro de ESC [ ... */
    uint32_t back = 0;                      /* posicion en el historial (0 = linea nueva) */
    char saved[SHELL_LINE_MAX];
    saved[0] = '\0';
    buf[0] = '\0';

    for (;;) {
        int c = shell_getc();

        if (esc == 1) {
            esc = (c == '[') ? 2 : 0;
            continue;
        }
        if (esc == 2) {
            if ((c >= '0' && c <= '9') || c == ';') {
                continue;                   /* parametros de la secuencia */
            }
            esc = 0;                        /* byte final */
            if (c == 'A' && back < g_hist_n) {              /* flecha arriba */
                if (back == 0u) {
                    copy_str(saved, buf, SHELL_LINE_MAX);
                }
                back++;
                copy_str(buf, history_back(back), SHELL_LINE_MAX);
                len = 0;
                while (buf[len] != '\0') {
                    len++;
                }
                redraw(buf);
            } else if (c == 'B' && back > 0u) {             /* flecha abajo */
                back--;
                copy_str(buf, (back == 0u) ? saved : history_back(back), SHELL_LINE_MAX);
                len = 0;
                while (buf[len] != '\0') {
                    len++;
                }
                redraw(buf);
            }
            continue;
        }

        if (c == 0x1B) {
            esc = 1;
        } else if (c == '\r' || c == '\n') {
            if (c == '\n' && last_was_cr) {
                last_was_cr = 0;
                continue;                   /* el \n de un "\r\n" */
            }
            last_was_cr = (c == '\r');
            kprintf("\r\n");
            buf[len] = '\0';
            return len;
        } else if (c == 0x08 || c == 0x7F) {                /* backspace */
            last_was_cr = 0;
            if (len > 0u) {
                len--;
                buf[len] = '\0';
                kprintf("\b \b");
            }
        } else if (c == 0x03) {                             /* Ctrl+C */
            last_was_cr = 0;
            kprintf("^C\r\n");
            buf[0] = '\0';
            return 0;
        } else if (c >= 0x20 && c < 0x7F) {
            last_was_cr = 0;
            if (len + 1u < SHELL_LINE_MAX) {
                buf[len++] = (char)c;
                buf[len] = '\0';
                uart_putc((char)c);
            }                               /* si esta llena, se ignora (sin desbordar) */
        }
    }
}

/* ---- ejecucion ---- */

static void execute(char *line) {
    char *argv[SHELL_MAX_ARGS + 1];
    int argc = 0;

    char *p = line;
    while (*p != '\0') {
        while (*p == ' ' || *p == '\t') {
            *p++ = '\0';
        }
        if (*p == '\0') {
            break;
        }
        if (argc >= (int)SHELL_MAX_ARGS) {
            kprintf("sentinel: demasiados argumentos (maximo %u)\r\n", SHELL_MAX_ARGS);
            g_last_status = 2;
            return;
        }
        argv[argc++] = p;
        while (*p != '\0' && *p != ' ' && *p != '\t') {
            p++;
        }
    }
    if (argc == 0) {
        return;
    }
    argv[argc] = NULL;

    for (uint32_t i = 0; i < g_ncmds; i++) {
        if (str_eq(g_cmds[i].name, argv[0])) {
            g_last_status = g_cmds[i].fn(argc, argv);
            return;
        }
    }
    kprintf("sentinel: comando no encontrado: %s\r\n", argv[0]);
    g_last_status = 127;
}

static void shell_task(void *arg) {
    (void)arg;
    char line[SHELL_LINE_MAX];

    vfs_chdir("/home/iker");
    sem_init(&g_rx_sem, 0);
#if SENTINEL_TRANSPORT
    downlink_init(&g_downlink);
    g_command_len = 0u;
    g_command_pos = 0u;
#endif
    sched_add_tick_hook(rx_poll_hook);

    kprintf("\r\nSentinel OS v0.1 - shell (escribe help)\r\n");
    char *motd[2] = {"cat", "/etc/motd"};
    for (uint32_t i = 0; i < g_ncmds; i++) {
        if (str_eq(g_cmds[i].name, "cat")) {
            g_cmds[i].fn(2, motd);
        }
    }

    for (;;) {
        print_prompt();
        if (read_line(line) > 0u) {
            history_add(line);
            execute(line);
        }
    }
}

int32_t shell_start(void) {
    shell_register_builtins();
    int32_t tid = task_create("shell", shell_task, (void *)0, 4, 0);
    if (tid >= 0) {
        g_shell_tid = (uint32_t)tid;
    }
    return tid;
}
