/* VFS + RAMFS: arbol n-ario con lista de hermanos, todo en el heap (kmalloc).
 *
 *      /
 *      +-- first_child --> bin --next_sibling--> etc --> home --> tmp
 *      |                                          |
 *      |                                          +-- first_child --> motd --> version
 *
 * Cada nodo guarda solo 3 punteros (padre, primer hijo, siguiente hermano) en vez de un
 * arreglo de tamano variable. Los hijos se mantienen ORDENADOS por nombre, asi `ls` sale
 * estable. Todo es iterativo (sin recursion): la pila de una tarea es de 4 KB.
 *
 * Portable: con -DVFS_HOST compila en el PC (tests/test_vfs.c) usando el mismo heap. */

#include <sentinel/vfs.h>
#include <sentinel/heap.h>
#include <sentinel/task.h>

#ifdef VFS_HOST

static task_fd_t g_host_fds[TASK_MAX_FDS];
static uint32_t g_host_clock;

static task_fd_t *cur_fds(void) { return g_host_fds; }
static uint32_t vfs_now(void) { return ++g_host_clock; }

/* En el PC no hay interrupciones ni tareas: la ventana atomica no hace nada. */
static uint32_t atomic_begin(void) { return 0; }
static void atomic_end(uint32_t ps) { (void)ps; }

#else

#include "../core/sched.h"
#include "../core/time.h"
#include "../arch/xtensa/cpu.h"

/* Ventana atomica (interrupciones apagadas, MUY corta): hace indivisible "buscar el nodo y
 * comprobar/tomar su referencia" o "buscarlo, comprobar que esta libre y desenlazarlo". Sin
 * esto, una tarea apropiada entre la busqueda y el hold dejaria que otra borrara el nodo y
 * el hold escribiria en memoria liberada. La liberacion de memoria ocurre FUERA de la ventana. */
static uint32_t atomic_begin(void) { return irq_save(); }
static void atomic_end(uint32_t ps) { irq_restore(ps); }

static task_fd_t g_boot_fds[TASK_MAX_FDS];

/* Antes de sched_init() (o desde el contexto de arranque) se usa una tabla propia. */
static task_fd_t *cur_fds(void) {
    tcb_t *t = sched_current();
    return (t != NULL) ? t->fds : g_boot_fds;
}
static uint32_t vfs_now(void) { return uptime_ms(); }

#endif

static vnode_t *g_root;

/* ---- cadenas (no hay libc) ---- */

static uint32_t str_len(const char *s) {
    uint32_t n = 0;
    while (s[n] != '\0') {
        n++;
    }
    return n;
}

static int str_cmp(const char *a, const char *b) {
    while (*a != '\0' && *a == *b) {
        a++;
        b++;
    }
    return (int)(unsigned char)*a - (int)(unsigned char)*b;
}

static int str_eq(const char *a, const char *b) {
    return str_cmp(a, b) == 0;
}

static void mem_zero(void *dst, uint32_t n) {
    uint8_t *d = (uint8_t *)dst;
    for (uint32_t i = 0; i < n; i++) {
        d[i] = 0;
    }
}

static void mem_copy(void *dst, const void *src, uint32_t n) {
    uint8_t *d = (uint8_t *)dst;
    const uint8_t *s = (const uint8_t *)src;
    for (uint32_t i = 0; i < n; i++) {
        d[i] = s[i];
    }
}

const char *vfs_strerror(int code) {
    switch (code) {
    case VFS_OK:           return "correcto";
    case VFS_ENOENT:       return "no existe";
    case VFS_EEXIST:       return "ya existe";
    case VFS_ENOTDIR:      return "no es un directorio";
    case VFS_EISDIR:       return "es un directorio";
    case VFS_ENOTEMPTY:    return "el directorio no esta vacio";
    case VFS_ENOMEM:       return "sin memoria";
    case VFS_EINVAL:       return "argumento invalido";
    case VFS_EBUSY:        return "en uso";
    case VFS_ENAMETOOLONG: return "nombre demasiado largo (maximo 31)";
    case VFS_ETOODEEP:     return "demasiada profundidad (maximo 16 niveles)";
    case VFS_EBADF:        return "descriptor invalido";
    case VFS_ENFILE:       return "sin descriptores libres";
    default:               return "error desconocido";
    }
}

/* ---- nodos ---- */

static vnode_t *new_node(const char *name, vnode_type_t type) {
    vnode_t *n = (vnode_t *)kmalloc_owner((uint32_t)sizeof(vnode_t), 0);   /* del kernel: kill no lo libera */
    if (n == NULL) {
        return NULL;
    }
    mem_zero(n, (uint32_t)sizeof(vnode_t));
    uint32_t i = 0;
    for (; name[i] != '\0' && i < VFS_NAME_MAX; i++) {
        n->name[i] = name[i];
    }
    n->name[i] = '\0';
    n->type = type;
    n->perms = (type == VN_DIR) ? (VFS_PERM_R | VFS_PERM_W | VFS_PERM_X)
                                : (VFS_PERM_R | VFS_PERM_W);
    n->created_tick = vfs_now();
    return n;
}

static void free_node_memory(vnode_t *n) {
    if (n->data != NULL) {
        kfree(n->data);
    }
    kfree(n);
}

static vnode_t *find_child(const vnode_t *dir, const char *name) {
    for (vnode_t *c = dir->first_child; c != NULL; c = c->next_sibling) {
        if (str_eq(c->name, name)) {
            return c;
        }
    }
    return NULL;
}

/* Inserta ordenado por nombre. */
static void link_child(vnode_t *dir, vnode_t *n) {
    vnode_t **pp = &dir->first_child;
    while (*pp != NULL && str_cmp((*pp)->name, n->name) < 0) {
        pp = &(*pp)->next_sibling;
    }
    n->next_sibling = *pp;
    n->parent = dir;
    *pp = n;
    dir->size++;
}

static void unlink_child(vnode_t *n) {
    vnode_t *dir = n->parent;
    vnode_t **pp = &dir->first_child;
    while (*pp != n) {
        pp = &(*pp)->next_sibling;
    }
    *pp = n->next_sibling;
    n->next_sibling = NULL;
    n->parent = NULL;
    dir->size--;
}

/* Libera n y todo lo que cuelga de el, sin recursion: siempre se desciende por
 * first_child; al liberar una hoja, el padre pasa a apuntar a su siguiente hermano.
 * n debe estar ya desenlazado de su padre. */
static void free_subtree(vnode_t *n) {
    vnode_t *cur = n;
    for (;;) {
        while (cur->first_child != NULL) {
            cur = cur->first_child;
        }
        vnode_t *parent = cur->parent;
        vnode_t *done = cur;
        if (done == n) {
            free_node_memory(done);
            return;
        }
        parent->first_child = done->next_sibling;
        free_node_memory(done);
        cur = parent;
    }
}

void vfs_destroy_node(vnode_t *n) {
    if (n != NULL) {
        free_subtree(n);
    }
}

static int subtree_busy(vnode_t *n) {
    vnode_t *cur = n;
    for (;;) {
        if (cur->refs > 0) {
            return 1;
        }
        if (cur->first_child != NULL) {
            cur = cur->first_child;
            continue;
        }
        while (cur != n && cur->next_sibling == NULL) {
            cur = cur->parent;
        }
        if (cur == n) {
            return 0;
        }
        cur = cur->next_sibling;
    }
}

static uint32_t depth_of(const vnode_t *n) {
    uint32_t d = 0;
    while (n->parent != NULL) {
        d++;
        n = n->parent;
    }
    return d;
}

void vfs_init(void) {
    g_root = new_node("", VN_DIR);   /* la raiz no tiene nombre; su ruta es "/" */
}

vnode_t *vfs_root(void) {
    return g_root;
}

void vfs_hold(vnode_t *n) {
    if (n != NULL && n->refs < 255u) {
        n->refs++;
    }
}

void vfs_release(vnode_t *n) {
    if (n != NULL && n->refs > 0u) {
        n->refs--;
    }
}

/* ---- resolucion de rutas (HU-E05-02) ---- */

/* Camina la ruta componente a componente. "." se ignora, ".." sube (en la raiz se queda
 * en la raiz), las barras repetidas o finales se toleran. */
static int walk(const char *path, vnode_t *cwd, vnode_t **out) {
    if (path == NULL || path[0] == '\0') {
        return VFS_EINVAL;
    }
    vnode_t *cur = (path[0] == '/') ? g_root : ((cwd != NULL) ? cwd : g_root);
    const char *p = path;

    while (*p != '\0') {
        while (*p == '/') {
            p++;
        }
        if (*p == '\0') {
            break;
        }
        char comp[VFS_NAME_MAX + 1];
        uint32_t n = 0;
        while (*p != '\0' && *p != '/') {
            if (n >= VFS_NAME_MAX) {
                return VFS_ENAMETOOLONG;
            }
            comp[n++] = *p++;
        }
        comp[n] = '\0';

        if (n == 1 && comp[0] == '.') {
            continue;
        }
        if (cur->type != VN_DIR) {
            return VFS_ENOTDIR;
        }
        if (n == 2 && comp[0] == '.' && comp[1] == '.') {
            if (cur->parent != NULL) {
                cur = cur->parent;
            }
            continue;
        }
        vnode_t *child = find_child(cur, comp);
        if (child == NULL) {
            return VFS_ENOENT;
        }
        cur = child;
    }
    *out = cur;
    return VFS_OK;
}

vnode_t *vfs_resolve(const char *path, vnode_t *cwd) {
    vnode_t *n = NULL;
    return (walk(path, cwd, &n) == VFS_OK) ? n : NULL;
}

/* Separa "dir/nombre" y resuelve el directorio padre. name recibe el ultimo componente. */
static int split_parent(const char *path, vnode_t *cwd, vnode_t **parent, char *name) {
    if (path == NULL || path[0] == '\0') {
        return VFS_EINVAL;
    }
    uint32_t len = str_len(path);
    if (len >= VFS_PATH_MAX) {
        return VFS_ENAMETOOLONG;
    }
    char buf[VFS_PATH_MAX];
    mem_copy(buf, path, len + 1u);

    while (len > 1 && buf[len - 1u] == '/') {   /* barras finales */
        buf[--len] = '\0';
    }
    int32_t slash = -1;
    for (uint32_t i = 0; i < len; i++) {
        if (buf[i] == '/') {
            slash = (int32_t)i;
        }
    }

    const char *base;
    vnode_t *dir;
    if (slash < 0) {
        base = buf;
        dir = (cwd != NULL) ? cwd : g_root;
    } else {
        base = buf + slash + 1;
        if (slash == 0) {
            dir = g_root;
        } else {
            buf[slash] = '\0';
            int rc = walk(buf, cwd, &dir);
            if (rc != VFS_OK) {
                return rc;
            }
        }
    }
    if (*base == '\0') {
        return VFS_EINVAL;                       /* la ruta era "/" */
    }
    if (str_eq(base, ".") || str_eq(base, "..")) {
        return VFS_EINVAL;
    }
    uint32_t blen = str_len(base);
    if (blen > VFS_NAME_MAX) {
        return VFS_ENAMETOOLONG;
    }
    if (dir->type != VN_DIR) {
        return VFS_ENOTDIR;
    }
    mem_copy(name, base, blen + 1u);
    *parent = dir;
    return VFS_OK;
}

int vfs_path_of(const vnode_t *n, char *buf, uint32_t cap) {
    if (n == NULL || buf == NULL || cap < 2u) {
        return VFS_EINVAL;
    }
    if (n->parent == NULL) {
        buf[0] = '/';
        buf[1] = '\0';
        return VFS_OK;
    }
    /* Se arma de atras hacia adelante: primero se mide, luego se escribe. */
    uint32_t total = 0;
    for (const vnode_t *c = n; c->parent != NULL; c = c->parent) {
        total += 1u + str_len(c->name);
    }
    if (total + 1u > cap) {
        return VFS_EINVAL;
    }
    uint32_t pos = total;
    buf[pos] = '\0';
    for (const vnode_t *c = n; c->parent != NULL; c = c->parent) {
        uint32_t l = str_len(c->name);
        pos -= l;
        mem_copy(buf + pos, c->name, l);
        buf[--pos] = '/';
    }
    return VFS_OK;
}

/* ---- directorios (HU-E05-03) ---- */

static int make_node(const char *path, vnode_t *cwd, vnode_type_t type, vnode_t **out) {
    vnode_t *parent = NULL;
    char name[VFS_NAME_MAX + 1];
    int rc = split_parent(path, cwd, &parent, name);
    if (rc != VFS_OK) {
        return rc;
    }
    if (find_child(parent, name) != NULL) {
        return VFS_EEXIST;
    }
    if (type == VN_DIR && depth_of(parent) + 1u > VFS_MAX_DEPTH) {
        return VFS_ETOODEEP;
    }
    vnode_t *n = new_node(name, type);
    if (n == NULL) {
        return VFS_ENOMEM;
    }
    link_child(parent, n);
    if (out != NULL) {
        *out = n;
    }
    return VFS_OK;
}

int vfs_mkdir(const char *path, vnode_t *cwd) {
    return make_node(path, cwd, VN_DIR, NULL);
}

int vfs_create(const char *path, vnode_t *cwd) {
    return make_node(path, cwd, VN_FILE, NULL);
}

int vfs_create_virtual(const char *path, vnode_t *cwd, vfs_gen_fn gen) {
    vnode_t *n = NULL;
    int rc = make_node(path, cwd, VN_FILE, &n);
    if (rc == VFS_OK) {
        n->gen = gen;
        n->perms = VFS_PERM_R;
    }
    return rc;
}

vnode_t *vfs_readdir(const vnode_t *dir, uint32_t index) {
    if (dir == NULL || dir->type != VN_DIR) {
        return NULL;
    }
    vnode_t *c = dir->first_child;
    while (c != NULL && index > 0u) {
        c = c->next_sibling;
        index--;
    }
    return c;
}

int vfs_rmdir(const char *path, vnode_t *cwd) {
    vnode_t *n = NULL;
    uint32_t ps = atomic_begin();
    int rc = walk(path, cwd, &n);
    if (rc == VFS_OK) {
        if (n->parent == NULL) {
            rc = VFS_EBUSY;                       /* la raiz no se borra */
        } else if (n->type != VN_DIR) {
            rc = VFS_ENOTDIR;
        } else if (n->first_child != NULL) {
            rc = VFS_ENOTEMPTY;
        } else if (n->refs > 0u) {
            rc = VFS_EBUSY;
        } else {
            unlink_child(n);
        }
    }
    atomic_end(ps);
    if (rc == VFS_OK) {
        free_node_memory(n);
    }
    return rc;
}

int vfs_unlink(const char *path, vnode_t *cwd) {
    vnode_t *n = NULL;
    uint32_t ps = atomic_begin();
    int rc = walk(path, cwd, &n);
    if (rc == VFS_OK) {
        if (n->type == VN_DIR) {
            rc = VFS_EISDIR;
        } else if (n->refs > 0u) {
            rc = VFS_EBUSY;
        } else {
            unlink_child(n);
        }
    }
    atomic_end(ps);
    if (rc == VFS_OK) {
        free_node_memory(n);
    }
    return rc;
}

int vfs_rm_recursive(const char *path, vnode_t *cwd) {
    vnode_t *n = NULL;
    uint32_t ps = atomic_begin();
    int rc = walk(path, cwd, &n);
    if (rc == VFS_OK) {
        if (n->parent == NULL) {
            rc = VFS_EBUSY;
        } else if (subtree_busy(n)) {
            rc = VFS_EBUSY;                       /* algo adentro esta abierto o es un cwd */
        } else {
            unlink_child(n);
        }
    }
    atomic_end(ps);
    if (rc == VFS_OK) {
        free_subtree(n);                          /* ya desenlazado: nadie mas lo alcanza */
    }
    return rc;
}

/* ---- contenido de archivos (HU-E05-04) ---- */

#define VFS_GEN_BUF 512u

/* Asegura capacidad >= need. Si falla, el contenido anterior queda intacto. */
static int ensure_capacity(vnode_t *f, uint32_t need) {
    if (need <= f->capacity) {
        return VFS_OK;
    }
    uint32_t newcap = (f->capacity < 16u) ? 16u : f->capacity * 2u;
    if (newcap < need) {
        newcap = need;
    }
    uint8_t *nd = (uint8_t *)kmalloc_owner(newcap, 0);
    if (nd == NULL && newcap > need) {
        newcap = need;                            /* sin holgura: al menos lo justo */
        nd = (uint8_t *)kmalloc_owner(newcap, 0);
    }
    if (nd == NULL) {
        return VFS_ENOMEM;
    }
    mem_zero(nd, newcap);
    if (f->data != NULL) {
        mem_copy(nd, f->data, f->size);
        kfree(f->data);
    }
    f->data = nd;
    f->capacity = newcap;
    return VFS_OK;
}

int vfs_write(vnode_t *f, uint32_t off, const void *buf, uint32_t n) {
    if (f == NULL || (buf == NULL && n > 0u)) {
        return VFS_EINVAL;
    }
    if (f->type == VN_DIR) {
        return VFS_EISDIR;
    }
    if (f->gen != NULL) {
        return VFS_EINVAL;                        /* archivo virtual: solo lectura */
    }
    uint32_t end = off + n;
    if (end < off) {
        return VFS_EINVAL;                        /* desbordamiento */
    }
    if (n == 0u) {
        return 0;
    }
    int rc = ensure_capacity(f, end);
    if (rc != VFS_OK) {
        return rc;
    }
    for (uint32_t i = f->size; i < off; i++) {
        f->data[i] = 0;                           /* hueco entre el fin y off: ceros */
    }
    mem_copy(f->data + off, buf, n);
    if (end > f->size) {
        f->size = end;
    }
    return (int)n;
}

int vfs_read(vnode_t *f, uint32_t off, void *buf, uint32_t n) {
    if (f == NULL || (buf == NULL && n > 0u)) {
        return VFS_EINVAL;
    }
    if (f->type == VN_DIR) {
        return VFS_EISDIR;
    }
    if (f->gen != NULL) {
        char tmp[VFS_GEN_BUF];                    /* se genera en el momento de leer */
        uint32_t len = f->gen(tmp, VFS_GEN_BUF);
        if (len > VFS_GEN_BUF) {
            len = VFS_GEN_BUF;
        }
        if (off >= len) {
            return 0;
        }
        uint32_t k = (n < len - off) ? n : (len - off);
        mem_copy(buf, tmp + off, k);
        return (int)k;
    }
    if (off >= f->size) {
        return 0;
    }
    uint32_t k = (n < f->size - off) ? n : (f->size - off);
    mem_copy(buf, f->data + off, k);
    return (int)k;
}

int vfs_truncate(vnode_t *f) {
    if (f == NULL) {
        return VFS_EINVAL;
    }
    if (f->type == VN_DIR) {
        return VFS_EISDIR;
    }
    if (f->gen != NULL) {
        return VFS_EINVAL;
    }
    f->size = 0;
    return VFS_OK;
}

/* ---- descriptores por tarea (HU-E05-05) ---- */

static int fd_valid(const task_fd_t *fds, int fd) {
    return fd >= 0 && fd < (int)TASK_MAX_FDS && fds[fd].used;
}

static int open_locked(const char *path, uint32_t flags, vnode_t *cwd) {
    task_fd_t *fds = cur_fds();
    int slot = -1;
    for (int i = 0; i < (int)TASK_MAX_FDS; i++) {
        if (!fds[i].used) {
            slot = i;
            break;
        }
    }
    if (slot < 0) {
        return VFS_ENFILE;
    }

    vnode_t *n = NULL;
    int rc = walk(path, cwd, &n);
    if (rc == VFS_ENOENT && (flags & VFS_O_CREAT) != 0u) {
        rc = make_node(path, cwd, VN_FILE, &n);
    }
    if (rc != VFS_OK) {
        return rc;
    }
    if (n->type == VN_DIR) {
        return VFS_EISDIR;
    }
    uint32_t mode = flags & 3u;
    if (mode > VFS_O_RDWR) {
        return VFS_EINVAL;
    }
    if ((mode == VFS_O_WRONLY || mode == VFS_O_RDWR) && n->gen != NULL) {
        return VFS_EINVAL;
    }
    if ((flags & VFS_O_TRUNC) != 0u && mode != VFS_O_RDONLY) {
        vfs_truncate(n);
    }
    fds[slot].node = n;
    fds[slot].offset = 0;
    fds[slot].flags = (uint8_t)(flags & 0xFFu);
    vfs_hold(n);                                  /* primero la referencia... */
    fds[slot].used = 1;                           /* ...y solo despues el descriptor existe */
    return slot;
}

/* Toda la apertura (buscar, crear, tomar la referencia) es una unica ventana atomica. */
int vfs_open(const char *path, uint32_t flags, vnode_t *cwd) {
    uint32_t ps = atomic_begin();
    int rc = open_locked(path, flags, cwd);
    atomic_end(ps);
    return rc;
}

int vfs_close(int fd) {
    task_fd_t *fds = cur_fds();
    if (!fd_valid(fds, fd)) {
        return VFS_EBADF;
    }
    vfs_release((vnode_t *)fds[fd].node);
    fds[fd].node = NULL;
    fds[fd].used = 0;
    return VFS_OK;
}

int vfs_read_fd(int fd, void *buf, uint32_t n) {
    task_fd_t *fds = cur_fds();
    if (!fd_valid(fds, fd)) {
        return VFS_EBADF;
    }
    if ((fds[fd].flags & 3u) == VFS_O_WRONLY) {
        return VFS_EBADF;
    }
    int got = vfs_read((vnode_t *)fds[fd].node, fds[fd].offset, buf, n);
    if (got > 0) {
        fds[fd].offset += (uint32_t)got;
    }
    return got;
}

int vfs_write_fd(int fd, const void *buf, uint32_t n) {
    task_fd_t *fds = cur_fds();
    if (!fd_valid(fds, fd)) {
        return VFS_EBADF;
    }
    if ((fds[fd].flags & 3u) == VFS_O_RDONLY) {
        return VFS_EBADF;
    }
    vnode_t *node = (vnode_t *)fds[fd].node;
    if ((fds[fd].flags & VFS_O_APPEND) != 0u) {
        fds[fd].offset = node->size;
    }
    int put = vfs_write(node, fds[fd].offset, buf, n);
    if (put > 0) {
        fds[fd].offset += (uint32_t)put;
    }
    return put;
}

int vfs_seek(int fd, int32_t off, int whence) {
    task_fd_t *fds = cur_fds();
    if (!fd_valid(fds, fd)) {
        return VFS_EBADF;
    }
    vnode_t *node = (vnode_t *)fds[fd].node;
    int32_t base;
    switch (whence) {
    case VFS_SEEK_SET: base = 0; break;
    case VFS_SEEK_CUR: base = (int32_t)fds[fd].offset; break;
    case VFS_SEEK_END: base = (int32_t)node->size; break;
    default:           return VFS_EINVAL;
    }
    /* Sin aritmetica de 64 bits (no hay libgcc): se comprueba el desbordamiento a mano. */
    if (off > 0 && base > 0x7FFFFFFF - off) {
        return VFS_EINVAL;
    }
    if (off < 0 && (uint32_t)(-(off + 1)) + 1u > (uint32_t)base) {   /* sin negar INT32_MIN */
        return VFS_EINVAL;
    }
    int32_t pos = base + off;
    fds[fd].offset = (uint32_t)pos;
    return (int)pos;
}

#ifndef VFS_HOST
vnode_t *vfs_cwd(void) {
    tcb_t *t = sched_current();
    return (t != NULL && t->cwd_node != NULL) ? (vnode_t *)t->cwd_node : g_root;
}

int vfs_getcwd(char *buf, uint32_t cap) {
    return vfs_path_of(vfs_cwd(), buf, cap);
}

/* Copia informativa de la ruta en el TCB (cabe poco: se trunca). */
static void sync_cwd_text(tcb_t *t, const vnode_t *n) {
    char tmp[VFS_PATH_MAX];
    if (vfs_path_of(n, tmp, sizeof(tmp)) != VFS_OK) {
        return;
    }
    uint32_t i = 0;
    for (; tmp[i] != '\0' && i < TASK_CWD_LEN - 1u; i++) {
        t->cwd[i] = tmp[i];
    }
    t->cwd[i] = '\0';
}

int vfs_chdir(const char *path) {
    tcb_t *t = sched_current();
    if (t == NULL) {
        return VFS_EINVAL;
    }
    vnode_t *n = NULL;
    uint32_t ps = atomic_begin();
    int rc = walk(path, vfs_cwd(), &n);
    if (rc == VFS_OK && n->type != VN_DIR) {
        rc = VFS_ENOTDIR;
    }
    if (rc != VFS_OK) {
        atomic_end(ps);
        return rc;
    }
    vfs_hold(n);                                  /* primero el nuevo: nunca queda sin referencia */
    vfs_release((vnode_t *)t->cwd_node);
    t->cwd_node = n;
    atomic_end(ps);
    sync_cwd_text(t, n);
    return VFS_OK;
}

void vfs_task_init(struct tcb *t) {
    tcb_t *parent = sched_current();
    vnode_t *cwd = (parent != NULL && parent->cwd_node != NULL) ? (vnode_t *)parent->cwd_node : g_root;
    if (cwd != NULL) {
        vfs_hold(cwd);
        t->cwd_node = cwd;
        sync_cwd_text(t, cwd);
    }
}

void vfs_task_cleanup(struct tcb *t) {
    for (uint32_t i = 0; i < TASK_MAX_FDS; i++) {
        if (t->fds[i].used) {
            vfs_release((vnode_t *)t->fds[i].node);
            t->fds[i].node = NULL;
            t->fds[i].used = 0;
        }
    }
    if (t->cwd_node != NULL) {
        vfs_release((vnode_t *)t->cwd_node);
        t->cwd_node = NULL;
    }
}
#endif
