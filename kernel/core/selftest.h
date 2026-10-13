#ifndef SENTINEL_SELFTEST_H
#define SENTINEL_SELFTEST_H

#include <stdint.h>

/* Utilidades de las pruebas en placa. Los totales alimentan la linea RESUMEN del final:
 * el canal serie pierde bytes a veces, y asi el veredicto cabe en una linea corta. */
extern uint32_t g_n_pass;
extern uint32_t g_n_fail;

void check(const char *name, int ok);
void selftest_summary(void);

/* Pruebas por epica (cada una en su archivo). */
void test_e04(void);

#endif
