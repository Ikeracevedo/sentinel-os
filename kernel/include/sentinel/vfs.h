#ifndef SENTINEL_VFS_H
#define SENTINEL_VFS_H

#include <stdint.h>
#include <stddef.h>

/* Sistema de archivos en RAM (ADR-009): arbol n-ario de vnodes dentro del heap, asi crear
 * archivos consume memoria visible en `free`. Limites documentados:
 *   nombre <= 31 caracteres, ruta <= 127, profundidad <= 16 niveles. */
#define VFS_NAME_MAX   31
#define VFS_PATH_MAX   128
#define VFS_MAX_DEPTH  16

/* Codigos de error (negativos). vfs_strerror() da el texto en espanol. */
#define VFS_OK            0
#define VFS_ENOENT       (-1)
#define VFS_EEXIST       (-2)
#define VFS_ENOTDIR      (-3)
#define VFS_EISDIR       (-4)
#define VFS_ENOTEMPTY    (-5)
#define VFS_ENOMEM       (-6)
#define VFS_EINVAL       (-7)
#define VFS_EBUSY        (-8)
#define VFS_ENAMETOOLONG (-9)
#define VFS_ETOODEEP     (-10)
#define VFS_EBADF        (-11)
#define VFS_ENFILE       (-12)

/* Permisos simbolicos (no se verifican: no hay usuarios). */
#define VFS_PERM_R  4u
#define VFS_PERM_W  2u
#define VFS_PERM_X  1u

/* Flags de vfs_open. */
#define VFS_O_RDONLY  0x00u
#define VFS_O_WRONLY  0x01u
#define VFS_O_RDWR    0x02u
#define VFS_O_CREAT   0x10u
#define VFS_O_APPEND  0x20u
#define VFS_O_TRUNC   0x40u

#define VFS_SEEK_SET  0
#define VFS_SEEK_CUR  1
#define VFS_SEEK_END  2

typedef enum { VN_FILE = 0, VN_DIR = 1 } vnode_type_t;

/* Contenido generado al leer (archivos virtuales de /proc): escribe en buf (<= cap) y
 * devuelve cuantos bytes produjo. No se almacena nada. */
typedef uint32_t (*vfs_gen_fn)(char *buf, uint32_t cap);

typedef struct vnode {
    char          name[VFS_NAME_MAX + 1];
    vnode_type_t  type;
    uint32_t      size;           /* bytes (archivo) o numero de hijos (directorio) */
    uint8_t       perms;
    uint8_t       refs;           /* descriptores abiertos + tareas con este directorio como cwd */
    uint32_t      created_tick;
    struct vnode *parent;
    struct vnode *first_child;    /* si es DIR: hijos ordenados por nombre */
    struct vnode *next_sibling;
    uint8_t      *data;           /* si es FILE: kmalloc'd */
    uint32_t      capacity;
    vfs_gen_fn    gen;            /* != NULL: archivo virtual de solo lectura */
} vnode_t;

void     vfs_init(void);
vnode_t *vfs_root(void);
const char *vfs_strerror(int code);

/* Rutas absolutas ("/a/b"), relativas ("a/b", "./a") y con "..". Devuelve NULL si no
 * existe o es invalida; nunca modifica nada. cwd == NULL equivale a la raiz. */
vnode_t *vfs_resolve(const char *path, vnode_t *cwd);
/* Reconstruye la ruta absoluta de un nodo en buf. 0 si cabe, VFS_EINVAL si no. */
int      vfs_path_of(const vnode_t *n, char *buf, uint32_t cap);

int      vfs_mkdir(const char *path, vnode_t *cwd);
int      vfs_create(const char *path, vnode_t *cwd);          /* archivo vacio */
vnode_t *vfs_readdir(const vnode_t *dir, uint32_t index);     /* n-esimo hijo o NULL */
int      vfs_rmdir(const char *path, vnode_t *cwd);           /* solo si esta vacio */
int      vfs_unlink(const char *path, vnode_t *cwd);          /* borra un archivo */
int      vfs_rm_recursive(const char *path, vnode_t *cwd);    /* rm -r */
void     vfs_destroy_node(vnode_t *n);                        /* libera subarbol (sin desenlazar) */

/* Crea un archivo virtual cuyo contenido produce gen al leerlo. */
int      vfs_create_virtual(const char *path, vnode_t *cwd, vfs_gen_fn gen);

/* Contenido. Devuelven bytes escritos/leidos o un codigo de error (< 0). Si crecer el
 * archivo falla (VFS_ENOMEM) el contenido anterior queda intacto. */
int      vfs_write(vnode_t *f, uint32_t off, const void *buf, uint32_t n);
int      vfs_read(vnode_t *f, uint32_t off, void *buf, uint32_t n);
int      vfs_truncate(vnode_t *f);

/* Referencias: un nodo con refs > 0 no se puede borrar (EBUSY). */
void     vfs_hold(vnode_t *n);
void     vfs_release(vnode_t *n);

/* API tipo POSIX con descriptores por tarea (8 por tarea). */
int      vfs_open(const char *path, uint32_t flags, vnode_t *cwd);
int      vfs_close(int fd);
int      vfs_read_fd(int fd, void *buf, uint32_t n);
int      vfs_write_fd(int fd, const void *buf, uint32_t n);
int      vfs_seek(int fd, int32_t off, int whence);

#ifndef VFS_HOST
struct tcb;
/* Una tarea nueva hereda el directorio actual de quien la crea. */
void     vfs_task_init(struct tcb *t);
/* Al terminar una tarea: cierra sus descriptores y suelta su directorio actual. */
void     vfs_task_cleanup(struct tcb *t);

/* Directorio actual de la tarea que llama (la raiz si no tiene). */
vnode_t *vfs_cwd(void);
int      vfs_chdir(const char *path);                 /* 0 o error; solo directorios */
int      vfs_getcwd(char *buf, uint32_t cap);
#endif

#endif
