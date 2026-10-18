#ifndef SENTINEL_PROBE_H
#define SENTINEL_PROBE_H

/*
 * Las sondas son los puntos donde el kernel informa lo que acaba de hacer.
 * Por defecto estan activas. HU-E07-05 conectara SENTINEL_TRACE con
 * "make TRACE=0" para eliminarlas del binario.
 */
#ifndef SENTINEL_TRACE
#define SENTINEL_TRACE 1
#endif

/* Motivos de salida de CPU que viajan en arg1 de EV_SWITCH_OUT. */
enum {
    TRACE_SWITCH_QUANTUM = 1,
    TRACE_SWITCH_BLOCK   = 2,
    TRACE_SWITCH_YIELD   = 3,
    TRACE_SWITCH_PREEMPT = 4,
    TRACE_SWITCH_EXIT    = 5
};

#if SENTINEL_TRACE

#include <sentinel/trace.h>

/* prev y next son punteros a tcb_t; tid identifica a cada tarea. */
#define PROBE_SWITCH_OUT(prev, next, reason)                         \
    do {                                                              \
        trace_probe_emit(EV_SWITCH_OUT, (prev)->tid, (next)->tid, (reason)); \
    } while (0)

#define PROBE_SWITCH_IN(next, cycles, wait)                    \
    do {                                                        \
        trace_probe_emit(EV_SWITCH_IN, (next)->tid, (cycles), (wait)); \
    } while (0)

/* EV_ALLOC: bytes pedidos y direccion devuelta; cero indica fallo. */
#define PROBE_ALLOC(ptr, size, tid)                                  \
    do {                                                              \
        trace_probe_emit(EV_ALLOC, (uint8_t)(tid), (uint32_t)(size),         \
                   (uint32_t)(uintptr_t)(ptr));                       \
    } while (0)

/* EV_FREE: direccion y tamano del bloque antes de fusionarlo. */
#define PROBE_FREE_SIZED(ptr, size, tid)                             \
    do {                                                              \
        trace_probe_emit(EV_FREE, (uint8_t)(tid),                            \
                   (uint32_t)(uintptr_t)(ptr), (uint32_t)(size));      \
    } while (0)

/* EV_LOCK_TRY: la tarea actual intenta tomar el mutex. */
#define PROBE_LOCK_TRY(m)                                             \
    do {                                                              \
        trace_probe_emit(EV_LOCK_TRY, sched_current()->tid, (m)->id, 0u);   \
    } while (0)

/* EV_LOCK_ACQ: arg1 contiene los ciclos que espero la tarea. */
#define PROBE_LOCK_ACQ(m, waited)                                    \
    do {                                                              \
        trace_probe_emit(EV_LOCK_ACQ, sched_current()->tid, (m)->id,        \
                   (uint32_t)(waited));                               \
    } while (0)

/* EV_LOCK_REL: arg1 contiene los ciclos que poseyo el mutex. */
#define PROBE_LOCK_REL(m, held)                                      \
    do {                                                              \
        trace_probe_emit(EV_LOCK_REL, sched_current()->tid, (m)->id,        \
                   (uint32_t)(held));                                 \
    } while (0)

/* EV_BLOCK: arg1 identifica al propietario actual del mutex. */
#define PROBE_BLOCK(m)                                                \
    do {                                                              \
        trace_probe_emit(EV_BLOCK, sched_current()->tid, (m)->id,           \
                   ((m)->owner != NULL) ? (uint32_t)(m)->owner->tid   \
                                        : UINT32_MAX);                \
    } while (0)

#else

/* Sin traza, los argumentos de las sondas no se evaluan. */
#define PROBE_SWITCH_OUT(prev, next, reason) do { } while (0)
#define PROBE_SWITCH_IN(next, cycles, wait)   do { } while (0)
#define PROBE_ALLOC(ptr, size, tid)           do { } while (0)
#define PROBE_FREE_SIZED(ptr, size, tid)      do { } while (0)
#define PROBE_LOCK_TRY(m)                     do { } while (0)
#define PROBE_LOCK_ACQ(m, waited)             do { } while (0)
#define PROBE_LOCK_REL(m, held)               do { } while (0)
#define PROBE_BLOCK(m)                        do { } while (0)

#endif

#endif /* SENTINEL_PROBE_H */
