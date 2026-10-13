/* Sincronizacion: mutex y semaforos con colas FIFO, y el registro del grafo de espera.
 *
 * Invariante del grafo (lo consume el detector de E10):
 *     arista  A -> B   <=>   A.waiting_on == m  y  m.owner == B
 * Por eso cada transicion actualiza waiting_on / owner / owned[] / state dentro de la
 * MISMA seccion critica y en este orden: waiting_on se fija ANTES de pasar a BLOCKED, y
 * se limpia en el mismo instante en que la tarea deja la cola.
 *
 * Los mutex se TRASPASAN: unlock entrega la propiedad al primero de la cola (owner =
 * esperador) en vez de soltarla y dejar que compitan. Asi no hay ventana en la que el
 * mutex este libre con gente esperando, ni inanicion entre esperadores. */

#include <sentinel/sync.h>
#include <sentinel/kprintf.h>
#include <sentinel/probe.h>
#include "sched.h"
#include "time.h"
#include "../arch/xtensa/cpu.h"

static mutex_t *g_registry[MAX_MUTEXES];
static uint32_t g_next_id = 1;

/* ---- cola FIFO ---- */

static void waitq_init(waitq_t *q) {
    q->head = 0;
    q->count = 0;
}

static void waitq_push(waitq_t *q, uint8_t tid) {
    q->tid[(q->head + q->count) % MAX_TASKS] = tid;
    q->count++;
}

static tcb_t *waitq_pop(waitq_t *q) {
    if (q->count == 0) {
        return NULL;
    }
    uint8_t tid = q->tid[q->head];
    q->head = (uint8_t)((q->head + 1u) % MAX_TASKS);
    q->count--;
    return task_get(tid);
}

static void waitq_remove(waitq_t *q, uint8_t tid) {
    for (uint32_t i = 0; i < q->count; i++) {
        if (q->tid[(q->head + i) % MAX_TASKS] == tid) {
            for (uint32_t j = i; j + 1 < q->count; j++) {
                q->tid[(q->head + j) % MAX_TASKS] = q->tid[(q->head + j + 1) % MAX_TASKS];
            }
            q->count--;
            return;
        }
    }
}

static void copy_name(char *dst, const char *src) {
    uint32_t i = 0;
    if (src != NULL) {
        for (; i < SYNC_NAME_LEN - 1u && src[i] != '\0'; i++) {
            dst[i] = src[i];
        }
    }
    dst[i] = '\0';
}

/* ---- mutex ---- */

void mutex_init(mutex_t *m, const char *name) {
    m->owner = NULL;
    waitq_init(&m->q);
    m->acquisitions = 0;
    m->acquired_at = 0;
    copy_name(m->name, name);

    CRITICAL_ENTER();
    m->id = g_next_id++;
    for (uint32_t i = 0; i < MAX_MUTEXES; i++) {
        if (g_registry[i] == NULL) {
            g_registry[i] = m;
            break;
        }
    }
    CRITICAL_EXIT();
}

void mutex_destroy(mutex_t *m) {
    CRITICAL_ENTER();
    for (uint32_t i = 0; i < MAX_MUTEXES; i++) {
        if (g_registry[i] == m) {
            g_registry[i] = NULL;
        }
    }
    CRITICAL_EXIT();
}

/* Entrega m a t. Con las interrupciones apagadas. */
static void mutex_grant(mutex_t *m, tcb_t *t) {
    m->owner = t;
    m->acquisitions++;
    m->acquired_at = cpu_cycles();
    if (t->owned_count < TASK_MAX_OWNED) {
        t->owned[t->owned_count++] = m;
    }
}

static int lock_common(mutex_t *m, int timed, uint32_t ms) {
    tcb_t *t = sched_current();
    PROBE_LOCK_TRY(m);

    uint32_t ps = irq_save();
    if (m->owner == t) {
        irq_restore(ps);
        kprintf("mutex_lock: '%s' ya es propietaria de '%s' (no es recursivo)\r\n", t->name, m->name);
        return -2;
    }
    if (m->owner == NULL) {
        mutex_grant(m, t);
        irq_restore(ps);
        PROBE_LOCK_ACQ(m, 0u);
        return 0;
    }
    if (timed && ms == 0u) {
        irq_restore(ps);
        return -1;
    }

    /* Bloquear. Orden obligatorio: waiting_on ANTES de BLOCKED (ver cabecera). */
    t->wait_started = cpu_cycles();
    t->waiting_on = m;
    t->waiting_kind = (uint8_t)WAIT_MUTEX;
    t->wait_result = 0;
    t->timed_wait = (uint8_t)(timed ? 1u : 0u);
    if (timed) {
        t->wake_at_tick = uptime_ms() + ms;
    }
    t->state = TASK_BLOCKED;
    waitq_push(&m->q, t->tid);
    PROBE_BLOCK(m);

    sched_yield();   /* vuelve cuando unlock nos traspaso el mutex, o al expirar el plazo */
    irq_restore(ps);

    if (t->wait_result < 0) {
        return -1;
    }
    PROBE_LOCK_ACQ(m, cpu_cycles() - t->wait_started);
    return 0;
}

int mutex_lock(mutex_t *m) {
    return lock_common(m, 0, 0);
}

int mutex_lock_timeout(mutex_t *m, uint32_t ms) {
    return lock_common(m, 1, ms);
}

int mutex_unlock(mutex_t *m) {
    tcb_t *t = sched_current();

    uint32_t ps = irq_save();
    if (m->owner != t) {
        tcb_t *o = m->owner;
        irq_restore(ps);
        kprintf("mutex_unlock: '%s' intento soltar '%s' que es de %s\r\n",
                t->name, m->name, (o != NULL) ? o->name : "nadie");
        return -1;
    }
    PROBE_LOCK_REL(m, cpu_cycles() - m->acquired_at);

    for (uint32_t i = 0; i < t->owned_count; i++) {
        if (t->owned[i] == m) {
            t->owned[i] = t->owned[t->owned_count - 1u];
            t->owned[t->owned_count - 1u] = NULL;
            t->owned_count--;
            break;
        }
    }

    tcb_t *w = waitq_pop(&m->q);
    if (w != NULL) {
        mutex_grant(m, w);
        w->waiting_on = NULL;
        w->waiting_kind = (uint8_t)WAIT_NONE;
        w->timed_wait = 0;
        w->wait_result = 0;
        sched_make_ready(w);
    } else {
        m->owner = NULL;
    }
    irq_restore(ps);

    if (w != NULL) {
        sched_preempt_check();   /* si el esperador es mas prioritario, que corra ya */
    }
    return 0;
}

/* ---- semaforos ---- */

void sem_init(sem_t *s, int32_t initial) {
    s->count = initial;
    waitq_init(&s->q);
}

static int sem_wait_common(sem_t *s, int timed, uint32_t ms) {
    tcb_t *t = sched_current();

    uint32_t ps = irq_save();
    if (s->count > 0) {
        s->count--;
        irq_restore(ps);
        return 0;
    }
    if (timed && ms == 0u) {
        irq_restore(ps);
        return -1;
    }

    t->wait_started = cpu_cycles();
    t->waiting_on = s;
    t->waiting_kind = (uint8_t)WAIT_SEM;
    t->wait_result = 0;
    t->timed_wait = (uint8_t)(timed ? 1u : 0u);
    if (timed) {
        t->wake_at_tick = uptime_ms() + ms;
    }
    t->state = TASK_BLOCKED;
    waitq_push(&s->q, t->tid);

    sched_yield();
    irq_restore(ps);
    return (t->wait_result < 0) ? -1 : 0;
}

int sem_wait(sem_t *s) {
    return sem_wait_common(s, 0, 0);
}

int sem_wait_timeout(sem_t *s, uint32_t ms) {
    return sem_wait_common(s, 1, ms);
}

/* Valido desde un ISR: solo toca colas y la lista READY (todo con interrupciones
 * apagadas) y en contexto de interrupcion NO cede: el despachador del tick decide el
 * cambio al terminar, mirando la prioridad del recien despertado. */
void sem_post(sem_t *s) {
    uint32_t ps = irq_save();
    tcb_t *w = waitq_pop(&s->q);
    if (w != NULL) {
        w->waiting_on = NULL;
        w->waiting_kind = (uint8_t)WAIT_NONE;
        w->timed_wait = 0;
        w->wait_result = 0;
        sched_make_ready(w);   /* el valor pasa directo al esperador: count no sube */
    } else {
        s->count++;
    }
    irq_restore(ps);

    if (w != NULL && !sched_in_interrupt()) {
        sched_preempt_check();
    }
}

/* ---- ganchos para el scheduler y task.c ---- */

static void leave_queue(tcb_t *t) {
    if (t->waiting_kind == WAIT_MUTEX) {
        waitq_remove(&((mutex_t *)t->waiting_on)->q, t->tid);
    } else if (t->waiting_kind == WAIT_SEM) {
        waitq_remove(&((sem_t *)t->waiting_on)->q, t->tid);
    }
    t->waiting_on = NULL;
    t->waiting_kind = (uint8_t)WAIT_NONE;
    t->timed_wait = 0;
}

void sync_expire_wait(tcb_t *t) {
    leave_queue(t);
    t->wait_result = -1;
    sched_make_ready(t);
}

void sync_cancel_wait(tcb_t *t) {
    leave_queue(t);
}

void sync_release_all(tcb_t *t) {
    while (t->owned_count > 0) {
        mutex_t *m = (mutex_t *)t->owned[--t->owned_count];
        t->owned[t->owned_count] = NULL;
        if (m == NULL || m->owner != t) {
            continue;
        }
        tcb_t *w = waitq_pop(&m->q);
        if (w != NULL) {
            mutex_grant(m, w);
            w->waiting_on = NULL;
            w->waiting_kind = (uint8_t)WAIT_NONE;
            w->timed_wait = 0;
            w->wait_result = 0;
            sched_make_ready(w);
        } else {
            m->owner = NULL;
        }
    }
}

/* ---- diagnostico ---- */

uint32_t sync_mutex_count(void) {
    uint32_t n = 0;
    for (uint32_t i = 0; i < MAX_MUTEXES; i++) {
        if (g_registry[i] != NULL) {
            n++;
        }
    }
    return n;
}

mutex_t *sync_mutex_at(uint32_t index) {
    return (index < MAX_MUTEXES) ? g_registry[index] : NULL;
}

typedef struct {
    uint32_t id;
    char     name[SYNC_NAME_LEN];
    int16_t  owner_tid;      /* -1 = libre */
    uint32_t acquisitions;
    uint8_t  nwait;
    uint8_t  waiters[MAX_TASKS];
} snap_mutex_t;

typedef struct {
    uint8_t  tid;
    uint8_t  kind;
    uint32_t target_id;      /* id del mutex (0 si es semaforo) */
    int16_t  owner_tid;      /* dueno del mutex esperado, -1 si no aplica */
} snap_wait_t;

/* Toma una foto coherente bajo candado (cortisima: <= 16 mutex y 16 tareas) y la
 * imprime DESPUES, con las interrupciones abiertas. */
void sync_dump(void) {
    snap_mutex_t sm[MAX_MUTEXES];
    snap_wait_t sw[MAX_TASKS];
    uint32_t nm = 0;
    uint32_t nw = 0;

    CRITICAL_ENTER();
    for (uint32_t i = 0; i < MAX_MUTEXES; i++) {
        mutex_t *m = g_registry[i];
        if (m == NULL) {
            continue;
        }
        snap_mutex_t *e = &sm[nm++];
        e->id = m->id;
        copy_name(e->name, m->name);
        e->owner_tid = (m->owner != NULL) ? (int16_t)m->owner->tid : (int16_t)-1;
        e->acquisitions = m->acquisitions;
        e->nwait = m->q.count;
        for (uint32_t k = 0; k < m->q.count; k++) {
            e->waiters[k] = m->q.tid[(m->q.head + k) % MAX_TASKS];
        }
    }
    for (uint32_t i = 0; i < MAX_TASKS; i++) {
        tcb_t *t = task_get(i);
        if (t == NULL || t->waiting_kind == WAIT_NONE) {
            continue;
        }
        snap_wait_t *e = &sw[nw++];
        e->tid = t->tid;
        e->kind = t->waiting_kind;
        e->target_id = 0;
        e->owner_tid = -1;
        if (t->waiting_kind == WAIT_MUTEX) {
            mutex_t *m = (mutex_t *)t->waiting_on;
            e->target_id = m->id;
            e->owner_tid = (m->owner != NULL) ? (int16_t)m->owner->tid : (int16_t)-1;
        }
    }
    CRITICAL_EXIT();

    kprintf("--- sync: %u mutex ---\r\n", nm);
    for (uint32_t i = 0; i < nm; i++) {
        const snap_mutex_t *e = &sm[i];
        tcb_t *o = (e->owner_tid >= 0) ? task_get((uint32_t)e->owner_tid) : NULL;
        kprintf("mutex %u '%s': dueno=%s(tid %d) adquisiciones=%u esperan=%u",
                e->id, e->name, (o != NULL) ? o->name : "-", (int32_t)e->owner_tid,
                e->acquisitions, (uint32_t)e->nwait);
        for (uint32_t k = 0; k < e->nwait; k++) {
            tcb_t *w = task_get(e->waiters[k]);
            kprintf("%s%s(tid %u)", (k == 0) ? " [" : ", ", (w != NULL) ? w->name : "?",
                    (uint32_t)e->waiters[k]);
        }
        kprintf("%s\r\n", (e->nwait > 0) ? "]" : "");
    }
    kprintf("--- grafo de espera: %u tareas bloqueadas ---\r\n", nw);
    for (uint32_t i = 0; i < nw; i++) {
        const snap_wait_t *e = &sw[i];
        tcb_t *w = task_get(e->tid);
        if (e->kind == WAIT_MUTEX) {
            tcb_t *o = (e->owner_tid >= 0) ? task_get((uint32_t)e->owner_tid) : NULL;
            kprintf("%s(tid %u) -> mutex %u -> %s(tid %d)\r\n",
                    (w != NULL) ? w->name : "?", (uint32_t)e->tid, e->target_id,
                    (o != NULL) ? o->name : "-", (int32_t)e->owner_tid);
        } else {
            kprintf("%s(tid %u) espera un semaforo\r\n", (w != NULL) ? w->name : "?",
                    (uint32_t)e->tid);
        }
    }
}
