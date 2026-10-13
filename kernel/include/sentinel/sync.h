#ifndef SENTINEL_SYNC_H
#define SENTINEL_SYNC_H

#include <stdint.h>
#include <sentinel/task.h>

#define MAX_MUTEXES    16
#define SYNC_NAME_LEN  16

/* Plazos mayores que esto (~24,8 dias) se recortan: el tick compara con resta con signo. */
#define SYNC_MAX_TIMEOUT_MS  0x7FFFFFFFu

#define WAIT_NONE   0u
#define WAIT_MUTEX  1u
#define WAIT_SEM    2u

/* Cola FIFO de tareas esperando (guarda tids). Una tarea espera como maximo en una cola. */
typedef struct {
    uint8_t tid[MAX_TASKS];
    uint8_t head;
    uint8_t count;
} waitq_t;

typedef struct mutex {
    uint32_t id;                 /* unico, para referenciarlo en eventos (E07) */
    char     name[SYNC_NAME_LEN];
    tcb_t   *owner;              /* NULL si esta libre */
    waitq_t  q;
    uint32_t acquisitions;
    uint32_t acquired_at;        /* CCOUNT al adquirirlo */
} mutex_t;

typedef struct semaphore {
    int32_t count;
    waitq_t q;
} sem_t;

/* Mutex no recursivo, cola FIFO (ordenar por prioridad llega en E18), sin herencia de
 * prioridad (a proposito: la inversion se provoca en E09 y se resuelve en E18).
 * Solo desde tareas: bloquean. */
void mutex_init(mutex_t *m, const char *name);
int  mutex_destroy(mutex_t *m);                    /* 0 ok, -1 si esta en uso (dueno o esperadores) */
int  mutex_lock(mutex_t *m);                        /* 0 ok, -2 reentrada, -3 ya posee el maximo (8) */
int  mutex_lock_timeout(mutex_t *m, uint32_t ms);   /* 0 ok, -1 expiro, -2 reentrada */
int  mutex_unlock(mutex_t *m);                      /* 0 ok, -1 si quien llama no es el dueno */

/* Semaforo contador. sem_post es seguro desde un ISR (no cede la CPU en ese contexto). */
void sem_init(sem_t *s, int32_t initial);
int  sem_wait(sem_t *s);                            /* 0 */
int  sem_wait_timeout(sem_t *s, uint32_t ms);       /* 0 ok, -1 expiro */
void sem_post(sem_t *s);

/* Los llama el scheduler / task.c. Siempre con las interrupciones apagadas. */
void sync_expire_wait(tcb_t *t);   /* plazo vencido: saca a t de la cola y la despierta (-1) */
void sync_cancel_wait(tcb_t *t);   /* t muere esperando: solo la saca de la cola */
void sync_release_all(tcb_t *t);   /* t termina: suelta (con traspaso) todos sus mutex */

/* Diagnostico (materia prima de E10). */
uint32_t sync_mutex_count(void);
mutex_t *sync_mutex_at(uint32_t index);       /* NULL si el hueco esta libre */
void sync_dump(void);                         /* quien posee que y quien espera a quien */

#endif
