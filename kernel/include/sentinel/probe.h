#ifndef SENTINEL_PROBE_H
#define SENTINEL_PROBE_H

/* Puntos de enganche para la instrumentacion (E07). Hoy estan vacios: evaluan
 * sus argumentos (para no dejar variables "sin usar") y el compilador los
 * elimina. E07 solo reescribe estas macros, sin tocar la logica del kernel:
 * separacion entre mecanismo e instrumentacion. */

#define PROBE_SWITCH_OUT(prev, next, reason) \
    do { (void)(prev); (void)(next); (void)(reason); } while (0)

#define PROBE_SWITCH_IN(next, cycles, wait) \
    do { (void)(next); (void)(cycles); (void)(wait); } while (0)

#define PROBE_ALLOC(ptr, size, tid) \
    do { (void)(ptr); (void)(size); (void)(tid); } while (0)

#define PROBE_FREE(ptr, tid) \
    do { (void)(ptr); (void)(tid); } while (0)

/* E04: sincronizacion. 'waited' y 'held' son ciclos de CCOUNT. */
#define PROBE_LOCK_TRY(m) \
    do { (void)(m); } while (0)

#define PROBE_LOCK_ACQ(m, waited) \
    do { (void)(m); (void)(waited); } while (0)

#define PROBE_LOCK_REL(m, held) \
    do { (void)(m); (void)(held); } while (0)

#define PROBE_BLOCK(m) \
    do { (void)(m); } while (0)

#endif
