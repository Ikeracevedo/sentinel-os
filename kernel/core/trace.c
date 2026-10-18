#include <sentinel/trace.h>
#include <stddef.h>

#ifndef TRACE_HOST
#include "../arch/xtensa/cpu.h"
#else
static uint32_t g_host_cycles;

static uint32_t cpu_cycles(void) {
    return ++g_host_cycles;
}

static uint32_t irq_disable(void) {
    return 0;
}

static void irq_restore(uint32_t saved_ps) {
    (void)saved_ps;
}
#endif

#define TRACE_BUFFER_MASK (TRACE_BUFFER_CAPACITY - 1u)

_Static_assert((TRACE_BUFFER_CAPACITY & TRACE_BUFFER_MASK) == 0u,
               "La capacidad del buffer debe ser potencia de 2");

/*
 * head tiene un solo escritor: el productor, sea tarea o ISR en core 0.
 * tail tiene un solo escritor: el consumidor.
 * full_head es el valor de head que exige volver a consultar tail.
 * buffer contiene 2048 filas de 16 bytes en la misma base que los indices.
 */
typedef struct {
    volatile uint32_t head;
    uint32_t full_head;
    volatile uint32_t tail;
    volatile uint32_t dropped;
    uint32_t buffer[TRACE_BUFFER_CAPACITY][4];
} trace_state_t;

_Static_assert(sizeof(((trace_state_t *)0)->buffer) == 32768u,
               "El buffer de eventos debe ocupar 32 KB");
_Static_assert(offsetof(trace_state_t, buffer) == 16u,
               "Los indices deben ocupar el prefijo de 16 bytes");

static trace_state_t g_state;
volatile uint8_t trace_enabled;
volatile uint8_t trace_active_types[256];
static uint8_t g_configured_types[256];
volatile uint32_t trace_sample_rate;
static uint32_t g_sample_counter;

void trace_init(void) {
    /* Llamar una sola vez, antes de iniciar productores y consumidor. */
    g_state.head = 0;
    g_state.full_head = TRACE_BUFFER_CAPACITY;
    g_state.tail = 0;
    g_state.dropped = 0;
    trace_enabled = 1u;
    trace_sample_rate = 1u;
    g_sample_counter = 0u;
    for (uint32_t i = 0; i < 256u; i++) {
        g_configured_types[i] = 1u;
        trace_active_types[i] = 1u;
    }
#ifdef TRACE_HOST
    g_host_cycles = 0;
#endif
}

void trace_set_enabled(int enabled) {
    trace_enabled = enabled ? 1u : 0u;
    /* Configuracion infrecuente: al volver el comando todos los tipos
     * tienen el nuevo estado. Una ISR puede ver la transicion parcial. */
    for (uint32_t i = 0; i < 256u; i++) {
        trace_active_types[i] = enabled ? g_configured_types[i] : 0u;
    }
}

int trace_set_filter(uint8_t type, int enabled) {
    if (type == EV_HEARTBEAT && !enabled) {
        return -1; /* ADR-008: el latido nunca puede silenciarse. */
    }
    g_configured_types[type] = enabled ? 1u : 0u;
    trace_active_types[type] = (trace_enabled && enabled) ? 1u : 0u;
    return 0;
}

int trace_get_filter(uint8_t type) {
    return g_configured_types[type] != 0u;
}

int trace_set_sample_rate(uint32_t rate) {
    if (rate == 0u || rate > 65535u) {
        return -1;
    }
    uint32_t saved_ps = irq_disable();
    trace_sample_rate = rate;
    g_sample_counter = 0u;
    irq_restore(saved_ps);
    return 0;
}

int trace_sample_allows(void) {
    uint32_t saved_ps = irq_disable();
    uint32_t next = g_sample_counter + 1u;
    uint32_t rate = trace_sample_rate;
    if (next >= rate) {
        next = 0u;
    }
    g_sample_counter = next;
    irq_restore(saved_ps);
    return next == 0u;
}

/* Camino excepcional separado: el productor normal no debe saltar sobre el
 * codigo de buffer lleno en cada evento. Se llama con IRQ deshabilitadas. */
static __attribute__((noinline, cold))
int trace_refresh_full(trace_state_t *state, uint32_t head) {
    uint32_t tail = state->tail;
    __atomic_thread_fence(__ATOMIC_ACQUIRE);

    uint32_t full_head = tail + TRACE_BUFFER_CAPACITY;
    state->full_head = full_head;

    if (head == full_head) {
        state->dropped++;
        return 1;
    }
    return 0;
}

void trace_emit_packed(uint32_t type_tid, uint32_t arg0, uint32_t arg1) {
    /*
     * Una ISR no puede interrumpir a una tarea mientras esta publica
     * un evento. Restauramos el estado exacto al terminar.
     */
    uint32_t saved_ps = irq_disable();
    __atomic_signal_fence(__ATOMIC_SEQ_CST);

    trace_state_t *state = &g_state;
    uint32_t head;

#ifndef TRACE_HOST
    /*
     * El productor es el unico escritor de head. Esta lectura Xtensa
     * evita la barrera acquire que GCC agrega a una lectura volatile.
     */
    __asm__ __volatile__("l32i %0, %1, 0"
                         : "=r"(head)
                         : "r"(state)
                         : "memory");
#else
    head = state->head;
#endif

    /* La rama excepcional queda despues del retorno normal. La instruccion
     * beq no se toma mientras el buffer tiene espacio disponible. */
    uint32_t full_head = state->full_head;
#ifndef TRACE_HOST
    __asm__ goto("beq %0, %1, %l[check_full]"
                 : : "r"(head), "r"(full_head) : : check_full);
#else
    if (head == full_head) {
        goto check_full;
    }
#endif

write_slot:
    /* Dos instrucciones calculan (head modulo 2048) * 16. GCC transforma
     * la expresion C equivalente en tres desplazamientos en Xtensa. */
    uint32_t offset;
#ifndef TRACE_HOST
    __asm__ __volatile__("extui %0, %1, 0, 11\n\t"
                         "slli %0, %0, 4"
                         : "=&r"(offset) : "r"(head));
#else
    offset = (head & TRACE_BUFFER_MASK) << 4;
#endif
    /* Los 16 bytes iniciales de state son los indices y contadores. */
    uint32_t *slot = (uint32_t *)((uint8_t *)state + offset);

    slot[4] = cpu_cycles();
    /* El desplazamiento descarta por si solo los bits altos de head. */
    slot[5] = type_tid | (head << 16);
    slot[6] = arg0;
    slot[7] = arg1;

    uint32_t next = head + 1u;

    /*
     * Release: primero quedan escritas las cuatro palabras; despues
     * se publica el nuevo head para que el consumidor pueda leerlas.
     */
    __atomic_thread_fence(__ATOMIC_RELEASE);

#ifndef TRACE_HOST
    /*
     * La barrera anterior ya genero memw. Esta escritura directa evita
     * que GCC inserte una segunda memw por el miembro volatile.
     */
    __asm__ __volatile__("s32i %0, %1, 0"
                         :
                         : "r"(next), "r"(state)
                         : "memory");
#else
    state->head = next;
#endif

    __atomic_signal_fence(__ATOMIC_SEQ_CST);
    irq_restore(saved_ps);
#ifndef TRACE_HOST
    /* Xtensa usa ABI de ventanas: retw.n cierra esta llamada directamente.
     * Evita el salto que GCC inserta para compartir el retorno con la rama
     * excepcional; no altera la barrera ni el estado de interrupciones. */
    __asm__ __volatile__("retw.n" ::: "memory");
    __builtin_unreachable();
#else
    return;
#endif

check_full:
    if (trace_refresh_full(state, head)) {
        __atomic_signal_fence(__ATOMIC_SEQ_CST);
        irq_restore(saved_ps);
        return;
    }
    goto write_slot;
}

int trace_pop(trace_event_t *out) {
    if (out == NULL) {
        return 0;
    }

    uint32_t tail = g_state.tail;
    uint32_t head = g_state.head;

    if (tail == head) {
        return 0;
    }

    /* Leer el evento solo despues de observar el head publicado. */
    __atomic_thread_fence(__ATOMIC_ACQUIRE);

    const uint32_t *slot = g_state.buffer[tail & TRACE_BUFFER_MASK];
    uint32_t metadata = slot[1];

    out->cycles = slot[0];
    out->type = (uint8_t)metadata;
    out->tid = (uint8_t)(metadata >> 8);
    out->seq = (uint16_t)(metadata >> 16);
    out->arg0 = slot[2];
    out->arg1 = slot[3];

    /* No liberar el espacio hasta terminar de copiar el evento. */
    __atomic_thread_fence(__ATOMIC_RELEASE);
    g_state.tail = tail + 1u;
    return 1;
}

uint32_t trace_emitted(void) {
    return g_state.head;
}

uint32_t trace_dropped(void) {
    return g_state.dropped;
}

uint32_t trace_count(void) {
    uint32_t tail = g_state.tail;
    uint32_t head = g_state.head;
    return head - tail;
}

#ifndef TRACE_HOST
#include <sentinel/heap.h>
#include <sentinel/task.h>
#include "sched.h"

/*
 * Toma una muestra del heap desde una tarea, cada 100 ms.
 * heap_stats recorre los bloques del heap; hacerlo desde una ISR
 * alargaria innecesariamente la interrupcion del temporizador.
 */
void trace_heap_stats_task(void *arg) {
    (void)arg;

    for (;;) {
        task_sleep(100u);

        heap_stats_t stats;
        heap_stats(&stats);

        trace_probe_emit(EV_HEAP_STATS, sched_current()->tid,
                         stats.free, stats.largest_free_block);
    }
}
#endif
