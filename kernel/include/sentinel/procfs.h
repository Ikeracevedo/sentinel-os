#ifndef SENTINEL_PROCFS_H
#define SENTINEL_PROCFS_H

#include <stdint.h>
#include <sentinel/task.h>

/* Generadores de texto: los usan tanto los archivos virtuales de /proc como los comandos
 * del shell (free, uptime, uname), asi las dos vistas coinciden EXACTAMENTE. Escriben en
 * buf (maximo cap-1 caracteres + '\0') y devuelven la longitud. */
uint32_t meminfo_text(char *buf, uint32_t cap);
uint32_t tasks_text(char *buf, uint32_t cap);
uint32_t uptime_text(char *buf, uint32_t cap);
uint32_t version_text(char *buf, uint32_t cap);

const char *task_state_name(task_state_t s);

/* Crea /proc con meminfo, tasks, uptime y version (contenido generado al leer). */
void procfs_init(void);

/* Arbol de demostracion: /bin /etc/{motd,version} /home/iker/{readme.txt,notas} /proc /tmp. */
void vfs_populate_demo(void);
uint32_t vfs_demo_heap_bytes(void);   /* heap que consumio la precarga */

#endif
