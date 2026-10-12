#ifndef SENTINEL_TASK_H
#define SENTINEL_TASK_H

#include <stdint.h>
#include <stddef.h>

#define MAX_TASKS 16
#define TASK_NAME_LEN 16
#define TASK_MAX_OWNED 4
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
tcb_t *task_register_boot(const char *name);
int32_t task_create(const char *name, task_fn_t fn, void *arg,
                    uint8_t priority, uint32_t stack_size);
void task_exit(void) __attribute__((noreturn));

#endif