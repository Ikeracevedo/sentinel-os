#ifndef SENTINEL_SHELL_H
#define SENTINEL_SHELL_H

#include <stdint.h>

/* Un comando recibe (argc, argv) como en C estandar y devuelve su codigo de salida
 * (0 = bien). argv[0] es el nombre del comando. */
typedef int (*cmd_fn)(int argc, char **argv);

#define SHELL_LINE_MAX   128
#define SHELL_MAX_ARGS   8
#define SHELL_HISTORY    10

/* Otras epicas registran sus comandos aqui sin tocar shell.c (E07: trace, E09: inject...). */
int cmd_register(const char *name, cmd_fn fn, const char *brief, const char *usage);

/* Crea la tarea del shell (prioridad 4): una tarea mas del scheduler, con su tid y su pila. */
int32_t shell_start(void);
uint32_t shell_tid(void);
int shell_last_status(void);

/* Utilidades para los comandos. */
int shell_parse_uint(const char *s, uint32_t *out);
void shell_print_error(const char *cmd, const char *what, const char *arg);

/* Registra los comandos incorporados (cmds.c). */
void shell_register_builtins(void);

/* Tabla de comandos (para help). */
uint32_t shell_cmd_count(void);
const char *shell_cmd_name(uint32_t i);
const char *shell_cmd_brief(uint32_t i);
const char *shell_cmd_usage(uint32_t i);

/* Historial (para el comando history). */
uint32_t shell_history_count(void);
const char *shell_history_get(uint32_t i);   /* 0 = el mas antiguo */

#endif
