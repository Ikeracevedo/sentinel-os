#include "selftest.h"
#include <sentinel/kprintf.h>

uint32_t g_n_pass;
uint32_t g_n_fail;

#define MAX_FAIL_NAMES 24u
static const char *g_fail_names[MAX_FAIL_NAMES];

void check(const char *name, int ok) {
    if (ok) {
        g_n_pass++;
    } else {
        if (g_n_fail < MAX_FAIL_NAMES) {
            g_fail_names[g_n_fail] = name;
        }
        g_n_fail++;
    }
    kprintf("[%s] %s\r\n", ok ? "PASS" : "FAIL", name);
}

/* El canal serie pierde bytes a veces: el veredicto repite los nombres de los fallos
 * en un bloque corto al final, para que ninguno se pierda entre el ruido. */
void selftest_summary(void) {
    kprintf("RESUMEN: %u PASS, %u FAIL\r\n", g_n_pass, g_n_fail);
    for (uint32_t i = 0; i < g_n_fail && i < MAX_FAIL_NAMES; i++) {
        kprintf("FALLO: %s\r\n", g_fail_names[i]);
    }
}
