#include <assert.h>
#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <sentinel/graph.h>
#include <sentinel/sync.h>

static tcb_t tasks[MAX_TASKS];
static mutex_t mutexes[MAX_TASKS];
static uint32_t irq_depth;
static uint32_t saves;
static uint32_t restores;
static uint32_t clock_cycles;

tcb_t *task_get(uint32_t tid) {
    return (tid < MAX_TASKS && tasks[tid].state != TASK_UNUSED) ? &tasks[tid] : NULL;
}

uint32_t graph_host_cycles(void) { return ++clock_cycles; }
uint32_t graph_host_irq_save(void) {
    assert(irq_depth == 0u);
    irq_depth = 1u;
    saves++;
    return 0x1234u;
}
void graph_host_irq_restore(uint32_t saved) {
    assert(saved == 0x1234u && irq_depth == 1u);
    irq_depth = 0u;
    restores++;
}
void kprintf(const char *fmt, ...) {
    assert(irq_depth == 0u); /* UART nunca dentro de la seccion critica. */
    (void)fmt;
}

static void reset(void) {
    memset(tasks, 0, sizeof(tasks));
    memset(mutexes, 0, sizeof(mutexes));
    for (uint32_t i = 0; i < MAX_TASKS; i++) {
        tasks[i].tid = (uint8_t)i;
    }
}

static void wait_for(uint32_t waiter, uint32_t owner) {
    tasks[waiter].state = TASK_BLOCKED;
    tasks[waiter].waiting_kind = WAIT_MUTEX;
    tasks[waiter].waiting_on = &mutexes[waiter];
    if (tasks[owner].state == TASK_UNUSED) {
        tasks[owner].state = TASK_READY;
    }
    mutexes[waiter].owner = &tasks[owner];
    tasks[owner].owned[tasks[owner].owned_count++] = &mutexes[waiter];
}

static uint32_t count_edges(const graph_t *g) {
    uint32_t count = 0u;
    for (uint32_t i = 0; i < MAX_TASKS; i++) {
        for (uint32_t j = 0; j < MAX_TASKS; j++) {
            count += g->edge[i][j];
        }
    }
    return count;
}

int main(void) {
    graph_t g;
    reset();
    assert(graph_build(&g) > 0u);
    assert(count_edges(&g) == 0u);

    reset();
    wait_for(1u, 2u);
    assert(graph_build(&g) > 0u);
    assert(g.edge[1][2] == 1u && count_edges(&g) == 1u);

    reset();
    wait_for(1u, 2u);
    tasks[1].waiting_kind = WAIT_SEM;
    graph_build(&g);
    assert(count_edges(&g) == 0u);

    reset();
    wait_for(1u, 2u);
    tasks[1].state = TASK_READY;
    graph_build(&g);
    assert(count_edges(&g) == 0u);

    reset();
    wait_for(1u, 2u);
    tasks[2].owned_count = 0u;
    graph_build(&g);
    assert(count_edges(&g) == 0u);

    reset();
    wait_for(1u, 2u);
    mutexes[1].owner = NULL;
    graph_build(&g);
    assert(count_edges(&g) == 0u);

    reset();
    wait_for(1u, 2u);
    wait_for(2u, 1u);
    graph_build(&g);
    assert(g.edge[1][2] && g.edge[2][1] && count_edges(&g) == 2u);

    reset();
    wait_for(1u, 2u);
    wait_for(2u, 3u);
    wait_for(3u, 1u);
    graph_build(&g);
    assert(g.edge[1][2] && g.edge[2][3] && g.edge[3][1] && count_edges(&g) == 3u);

    reset();
    graph_dump();
    assert(saves == restores && irq_depth == 0u);
    puts("graph: matriz coherente, filtros y ciclos 2/3 OK");
    return 0;
}
