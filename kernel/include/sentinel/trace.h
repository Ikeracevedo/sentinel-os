#ifndef SENTINEL_TRACE_H
#define SENTINEL_TRACE_H

#include <stdint.h>

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
 * Al desbordarse vuelve a cero (wrap). A 160 MHz ocurre aproximadamente
 * cada 2^32 / 160000000 = 26,8 segundos.
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

#endif /* SENTINEL_TRACE_H */