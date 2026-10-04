#ifndef SENTINEL_TASK_H
#define SENTINEL_TASK_H

#include <stdint.h>
#include <stddef.h>

#define MAX_TASKS 16
#define TASK_NAME_LEN 16
#define TASK_MAX_OWNED 4
#define TASK_CWD_LEN 32
#define STACK_CANARY 0xDEADBEEFu

typedef enum {
    TASK_UNUSED = 0,
    TASK_READY,
    TASK_RUNNING,
    TASK_BLOCKED,
    TASK_SLEEPING,
    TASK_SUSPENDED,
    TASK_TERMINATED,
} task_state_t;


typedef struct tcb {
    uint32_t     sp;             /* Debe ser el primer campo: lo asume context.S */
    task_state_t state;
    uint8_t      tid;
    uint8_t      priority;       /* 0 = maxima, 7 = idle */
    uint32_t     quantum_ticks;
    char         name[TASK_NAME_LEN];
    uint32_t    *stack_base;     /* extremo bajo de la pila (aqui vive el canario) */
    uint32_t     stack_size;     /* en bytes */

    uint64_t     cpu_cycles;     /* 64 bits: CCOUNT de 32 da la vuelta cada ~107 s */
    uint32_t     switches_in;
    uint32_t     ready_since;
    uint32_t     max_ready_wait;

    void        *waiting_on;     
    void        *owned[TASK_MAX_OWNED];

    char         cwd[TASK_CWD_LEN];  /* para E05 (VFS) */
} tcb_t;

tcb_t *task_get(uint32_t tid);
tcb_t *task_alloc(void);

#endif