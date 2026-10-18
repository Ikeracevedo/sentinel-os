#include <sentinel/trace.h>
#include <sentinel/shell.h>
#include <sentinel/kprintf.h>
#include <stddef.h>

typedef struct {
    const char *name;
    uint8_t type;
} trace_name_t;

static const trace_name_t k_types[] = {
    {"switch_out", EV_SWITCH_OUT}, {"switch_in", EV_SWITCH_IN},
    {"alloc", EV_ALLOC}, {"free", EV_FREE},
    {"heap_stats", EV_HEAP_STATS}, {"lock_try", EV_LOCK_TRY},
    {"lock_acq", EV_LOCK_ACQ}, {"lock_rel", EV_LOCK_REL},
    {"block", EV_BLOCK}, {"task_create", EV_TASK_CREATE},
    {"task_exit", EV_TASK_EXIT}, {"heartbeat", EV_HEARTBEAT},
    {"alert", EV_ALERT}, {"inject", EV_INJECT}
};

static int same(const char *a, const char *b) {
    while (*a != '\0' && *a == *b) {
        a++;
        b++;
    }
    return *a == *b;
}

static int parse_type(const char *text, uint8_t *type) {
    for (uint32_t i = 0; i < sizeof(k_types) / sizeof(k_types[0]); i++) {
        if (same(text, k_types[i].name)) {
            *type = k_types[i].type;
            return 0;
        }
    }
    uint32_t value;
    if (shell_parse_uint(text, &value) == 0 && value <= 255u) {
        *type = (uint8_t)value;
        return 0;
    }
    return -1;
}

static void print_stats(void) {
    uint32_t emitted = trace_emitted();
    uint32_t dropped = trace_dropped();
    uint32_t occupied = trace_count();
    uint32_t total = emitted + dropped;
    uint32_t percent = 0u;
    if (total != 0u) {
        uint32_t scale = total / 42949672u + 1u;
        percent = ((dropped / scale) * 100u) / (total / scale);
    }
    kprintf("trace: %s, emitidos=%u perdidos=%u perdidos_pct=%u%%\r\n",
            trace_enabled ? "on" : "off", emitted, dropped, percent);
    kprintf("buffer=%u/%u, sample=%u\r\n", occupied,
            TRACE_BUFFER_CAPACITY, trace_sample_rate);
    kprintf("filtros activos:");
    for (uint32_t i = 0; i < sizeof(k_types) / sizeof(k_types[0]); i++) {
        if (trace_get_filter(k_types[i].type)) {
            kprintf(" %s", k_types[i].name);
        }
    }
    kprintf("\r\n");
}

static int cmd_trace(int argc, char **argv) {
    if (argc == 2 && same(argv[1], "on")) {
        trace_set_enabled(1);
        kprintf("trace: on (heartbeat siempre activo)\r\n");
        return 0;
    }
    if (argc == 2 && same(argv[1], "off")) {
        trace_set_enabled(0);
        kprintf("trace: off (heartbeat siempre activo)\r\n");
        return 0;
    }
    if (argc == 2 && same(argv[1], "stats")) {
        print_stats();
        return 0;
    }
    if (argc == 4 && same(argv[1], "filter")) {
        uint8_t type;
        if (parse_type(argv[2], &type) != 0) {
            shell_print_error("trace", "tipo desconocido", argv[2]);
            return 1;
        }
        int enabled;
        if (same(argv[3], "on")) {
            enabled = 1;
        } else if (same(argv[3], "off")) {
            enabled = 0;
        } else {
            shell_print_error("trace", "use on u off", argv[3]);
            return 1;
        }
        if (trace_set_filter(type, enabled) != 0) {
            shell_print_error("trace", "heartbeat es incondicional", NULL);
            return 1;
        }
        kprintf("trace: filtro %s %s\r\n", argv[2], argv[3]);
        return 0;
    }
    if (argc == 3 && same(argv[1], "sample")) {
        uint32_t rate;
        if (shell_parse_uint(argv[2], &rate) != 0 ||
            trace_set_sample_rate(rate) != 0) {
            shell_print_error("trace", "sample requiere N entre 1 y 65535", NULL);
            return 1;
        }
        kprintf("trace: 1 de cada %u eventos switch\r\n", rate);
        return 0;
    }
    shell_print_error("trace", "uso: trace on|off|stats|filter <tipo> on|off|sample <N>", NULL);
    return 1;
}

void trace_register_command(void) {
    (void)cmd_register("trace", cmd_trace, "controla la instrumentacion",
                       "trace on|off|stats|filter <tipo> on|off|sample <N>");
}
