#ifndef SENTINEL_TRACE_H
#define SENTINEL_TRACE_H

#include <stdint.h>

#define TRACE_BUFFER_CAPACITY 2048u

enum {
    EV_SWITCH_OUT  = 0x01,
    EV_SWITCH_IN   = 0x02,

    EV_ALLOC       = 0x10,
    EV_FREE        = 0x11,
    EV_HEAP_STATS  = 0x12,

    EV_LOCK_TRY    = 0x20,
    EV_LOCK_ACQ    = 0x21,
    EV_LOCK_REL    = 0x22,
    EV_BLOCK       = 0x23,

    EV_TASK_CREATE = 0x30,
    EV_TASK_EXIT   = 0x31,

    EV_HEARTBEAT   = 0x40,
    EV_ALERT       = 0x50,
    EV_INJECT      = 0x60
};

/*
 * cycles contiene CCOUNT, el contador de ciclos de CPU de 32 bits.
 * Al desbordarse vuelve a cero: cada ~26,8 s a 160 MHz o cada
 * ~107,4 s a los 40 MHz configurados actualmente en cpu.h.
 */
typedef struct __attribute__((packed, aligned(4))) {
    uint32_t cycles;
    uint8_t  type;
    uint8_t  tid;
    uint16_t seq;
    uint32_t arg0;
    uint32_t arg1;
} trace_event_t;

_Static_assert(sizeof(trace_event_t) == 16,
               "trace_event_t debe ocupar exactamente 16 bytes");
_Static_assert(_Alignof(trace_event_t) == 4,
               "trace_event_t debe estar alineado a 4 bytes");

/* Llamar antes de empezar a emitir o consumir eventos. */
void trace_init(void);

/*
 * Funcion interna: solo la funcion inline de abajo debe llamarla.
 * Sus 16 bits bajos contienen type y tid; el resto es cero.
 */
void trace_emit_packed(uint32_t type_tid, uint32_t arg0, uint32_t arg1);

/*
 * API para el resto del kernel. El compilador prepara type_tid en el
 * punto de llamada y puede resolverlo por completo si ambos son constantes.
 */
static inline __attribute__((always_inline))
void trace_emit(uint8_t type, uint8_t tid, uint32_t arg0, uint32_t arg1) {
    uint32_t type_tid = (uint32_t)type | ((uint32_t)tid << 8);
    trace_emit_packed(type_tid, arg0, arg1);
}

/* La ruta directa trace_emit queda reservada para eventos incondicionales
 * como EV_HEARTBEAT y para las pruebas del ring buffer. */
extern volatile uint8_t trace_enabled;
extern volatile uint8_t trace_active_types[256];
extern volatile uint32_t trace_sample_rate;

/* Esta comprobacion es la ruta rapida del filtro. Los tipos son constantes
 * en las sondas, por lo que el compilador resuelve el indice de la tabla. */
static inline __attribute__((always_inline))
int trace_filter_allows(uint8_t type) {
    uint32_t enabled;
#ifndef TRACE_HOST
    const volatile uint8_t *slot = &trace_active_types[type];
    /* Una lectura de byte basta; el filtro no publica datos del buffer. */
    __asm__ __volatile__("l8ui %0, %1, 0"
                         : "=r"(enabled) : "r"(slot) : "memory");
#else
    enabled = trace_active_types[type];
#endif
    return enabled;
}

int trace_sample_allows(void);
static inline __attribute__((always_inline))
int trace_probe_allows(uint8_t type) {
    if (!trace_filter_allows(type)) {
        return 0;
    }
    if ((type == EV_SWITCH_OUT || type == EV_SWITCH_IN) &&
        trace_sample_rate > 1u && !trace_sample_allows()) {
        return 0;
    }
    return 1;
}

/* Macro: descartar ANTES de evaluar tid, arg0 y arg1. */
#define trace_probe_emit(type, tid, arg0, arg1)             \
    do {                                                     \
        if (trace_probe_allows((uint8_t)(type))) {           \
            trace_emit((uint8_t)(type), (uint8_t)(tid),       \
                       (uint32_t)(arg0), (uint32_t)(arg1));  \
        }                                                    \
    } while (0)

void trace_set_enabled(int enabled);
int trace_set_filter(uint8_t type, int enabled);
int trace_get_filter(uint8_t type);
int trace_set_sample_rate(uint32_t rate);

#ifndef TRACE_HOST
void trace_register_command(void);
#endif

/* Consumidor unico: devuelve 1 si copio un evento en out; 0 si esta vacio. */
int trace_pop(trace_event_t *out);

/* Estadisticas para el futuro comando "trace stats". */
uint32_t trace_emitted(void);
uint32_t trace_dropped(void);
uint32_t trace_count(void);

#ifndef TRACE_HOST
/* Funcion que ejecutara la tarea periodica de estadisticas del heap. */
void trace_heap_stats_task(void *arg);
#endif

#endif /* SENTINEL_TRACE_H */
