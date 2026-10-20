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

uint32_t graph_host_cycles(void) {
    assert(irq_depth == 1u);
    return ++clock_cycles;
}
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

typedef struct { uint8_t from; uint8_t to; } edge_t;

static void expect_cycle(const edge_t *edges, uint32_t n,
                         const uint8_t *expected, uint32_t count) {
    graph_t g = {0};
    graph_cycle_t cycle;
    for (uint32_t i = 0; i < n; i++) {
        graph_add_edge(&g, edges[i].from, edges[i].to);
    }
    int found = graph_find_cycle(&g, &cycle);
    assert(found == (count > 0u));
    assert(cycle.count == count);
    for (uint32_t i = 0; i < count; i++) {
        assert(cycle.tid[i] == expected[i]);
    }
}

static void test_cycles(void) {
    expect_cycle(NULL, 0u, NULL, 0u);                                /* vacio */
    expect_cycle((edge_t[]){{0, 1}}, 1u, NULL, 0u);                  /* una espera */
    expect_cycle((edge_t[]){{0, 1}, {1, 2}, {2, 3}}, 3u, NULL, 0u); /* cadena */
    expect_cycle((edge_t[]){{0, 1}, {0, 2}, {1, 3}, {2, 3}},
                 4u, NULL, 0u);                                    /* diamante */
    expect_cycle((edge_t[]){{0, 1}, {1, 3}, {0, 2}, {2, 3}},
                 4u, NULL, 0u);                                    /* negro != ciclo */
    expect_cycle((edge_t[]){{4, 5}, {5, 4}}, 2u,
                 (uint8_t[]){4, 5}, 2u);                            /* 2 tareas */
    expect_cycle((edge_t[]){{1, 2}, {2, 3}, {3, 1}}, 3u,
                 (uint8_t[]){1, 2, 3}, 3u);                         /* 3 tareas */
    expect_cycle((edge_t[]){{0, 1}, {1, 2}, {2, 3}, {3, 0}}, 4u,
                 (uint8_t[]){0, 1, 2, 3}, 4u);                      /* ciclo largo */
    expect_cycle((edge_t[]){{0, 1}, {7, 8}, {8, 7}}, 3u,
                 (uint8_t[]){7, 8}, 2u);                            /* componentes */
    expect_cycle((edge_t[]){{1, 2}, {2, 1}, {6, 7}, {7, 6}}, 4u,
                 (uint8_t[]){1, 2}, 2u);                            /* dos ciclos */
    expect_cycle((edge_t[]){{9, 9}}, 1u,
                 (uint8_t[]){9}, 1u);                               /* autolazo sintetico */
    expect_cycle((edge_t[]){{0, 1}, {1, 2}, {2, 3}, {3, 2}}, 4u,
                 (uint8_t[]){2, 3}, 2u);                            /* cola hacia ciclo */

    edge_t full[MAX_TASKS];
    uint8_t expected[MAX_TASKS];
    for (uint32_t i = 0; i < MAX_TASKS; i++) {
        full[i] = (edge_t){(uint8_t)i, (uint8_t)((i + 1u) % MAX_TASKS)};
        expected[i] = (uint8_t)i;
    }
    expect_cycle(full, MAX_TASKS, expected, MAX_TASKS);             /* 16 tareas */
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
    test_cycles();
    puts("graph: matriz y DFS iterativo, 13 grafos sinteticos OK");
    return 0;
}
