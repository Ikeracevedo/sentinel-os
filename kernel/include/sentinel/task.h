#ifndef SENTINEL_TASK_H
#define SENTINEL_TASK_H

#include <stdint.h>
#include <stddef.h>

#define MAX_TASKS 16
#define TASK_NAME_LEN 16
#define TASK_MAX_OWNED 8
#define TASK_MAX_FDS 8
#define TASK_CWD_LEN 32
#define STACK_CANARY 0xDEADBEEFu

#define TASK_STACK_SIZE          4096u
#define TASK_PRIORITY_LEVELS     8u
#define TASK_DEFAULT_QUANTUM_TICKS 10u   /* 10 ms a 1000 Hz */

typedef void (*task_fn_t)(void *arg);

typedef enum {
    TASK_UNUSED = 0,
    TASK_READY,
    TASK_RUNNING,
    TASK_BLOCKED,
    TASK_SLEEPING,
    TASK_SUSPENDED,
    TASK_TERMINATED,
} task_state_t;


/* E05: descriptor de archivo por tarea. */
typedef struct {
    void    *node;     /* vnode_t* */
    uint32_t offset;
    uint8_t  flags;
    uint8_t  used;
} task_fd_t;

typedef struct tcb {
    uint32_t     sp;             /* Debe ser el primer campo: lo asume context.S */
    task_state_t state;
    uint8_t      tid;
    uint8_t      priority;       /* 0 = maxima, 7 = idle */
    uint32_t     quantum_ticks;  /* cuanto configurado, en ticks (1 tick = 1 ms) */
    uint32_t     quantum_left;   /* ticks que le quedan en su turno actual */
    uint32_t     wake_at_tick;   /* SLEEPING: tick (ms) en que debe despertar */
    char         name[TASK_NAME_LEN];
    uint32_t    *stack_base;     /* extremo bajo de la pila (aqui vive el canario) */
    uint32_t     stack_size;     /* en bytes */

    uint64_t     cpu_cycles;     /* 64 bits: CCOUNT de 32 da la vuelta cada ~107 s */
    uint32_t     switches_in;
    uint32_t     ready_since;
    uint32_t     max_ready_wait;

    /* E04: grafo de espera. arista A -> B  <=>  A.waiting_on == m  y  m.owner == B.
     * waiting_on se fija ANTES de pasar a BLOCKED y se limpia al despertar o expirar. */
    void        *waiting_on;     /* mutex_t* o sem_t* que espera (segun waiting_kind), o NULL */
    uint8_t      waiting_kind;   /* 0 = nada, 1 = mutex, 2 = semaforo */
    uint8_t      owned_count;
    uint8_t      timed_wait;     /* BLOCKED con plazo (wake_at_tick) */
    int8_t       wait_result;    /* 0 = adquirido, -1 = expiro */
    uint32_t     wait_started;   /* CCOUNT al empezar a esperar (PROBE_LOCK_ACQ) */
    void        *owned[TASK_MAX_OWNED];

    char         cwd[TASK_CWD_LEN];  /* ruta en texto, solo informativa */
    void        *cwd_node;           /* E05: vnode_t* del directorio actual */
    task_fd_t    fds[TASK_MAX_FDS];  /* E05: descriptores de archivo */
} tcb_t;

tcb_t *task_get(uint32_t tid);
tcb_t *task_alloc(void);
tcb_t *task_register_boot(const char *name);
int32_t task_create(const char *name, task_fn_t fn, void *arg,
                    uint8_t priority, uint32_t stack_size);
void task_exit(void) __attribute__((noreturn));
void task_sleep(uint32_t ms);
int32_t task_suspend(uint32_t tid);
int32_t task_resume(uint32_t tid);
int32_t task_kill(uint32_t tid);   /* termina otra tarea: libera mutex, cola y bloques del heap */
void task_free(tcb_t *t);   /* libera el TCB y sus recursos; lo llama el scheduler */

#endif