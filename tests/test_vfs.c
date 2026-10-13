/* Tests del VFS en el PC: vfs.c y heap.c se compilan sin modificar (-DVFS_HOST -DHEAP_HOST).
 * Uso: make test */

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sentinel/heap.h>
#include <sentinel/vfs.h>
#include <sentinel/task.h>

/* ---- ganchos que heap.c espera en modo host ---- */
uint8_t heap_host_tid = 0;
void (*heap_host_preempt_hook)(void);
void heap_host_panic(const char *msg) {
    printf("PANIC inesperado: %s\n", msg);
    exit(2);
}

/* ---- mini framework ---- */
static int g_total;
static int g_failed;

#define CHECK(cond, name) do {                                        \
        g_total++;                                                    \
        if (cond) {                                                   \
            printf("[PASS] %s\n", name);                              \
        } else {                                                      \
            g_failed++;                                               \
            printf("[FAIL] %s  (%s:%d)\n", name, __FILE__, __LINE__); \
        }                                                             \
    } while (0)

static void fresh(void) {
    heap_init();
    vfs_init();
}

static heap_stats_t hs(void) {
    heap_stats_t s;
    heap_stats(&s);
    return s;
}

static int same_heap(heap_stats_t a, heap_stats_t b) {
    return a.free == b.free && a.block_count == b.block_count && a.used == b.used;
}

/* ---- casos ---- */

static void test_root_and_paths(void) {
    fresh();
    char buf[64];
    CHECK(vfs_root() != NULL && vfs_root()->type == VN_DIR, "init: existe la raiz y es un directorio");
    CHECK(vfs_resolve("/", NULL) == vfs_root(), "resolve('/') es la raiz");
    CHECK(vfs_resolve("", NULL) == NULL && vfs_resolve(NULL, NULL) == NULL, "ruta vacia o NULL = NULL");
    CHECK(vfs_path_of(vfs_root(), buf, sizeof(buf)) == 0 && strcmp(buf, "/") == 0, "path_of(raiz) = '/'");

    CHECK(vfs_mkdir("/home", NULL) == 0, "mkdir /home");
    CHECK(vfs_mkdir("/home/iker", NULL) == 0, "mkdir /home/iker");
    CHECK(vfs_mkdir("/home/iker/notas", NULL) == 0, "mkdir /home/iker/notas (3 niveles)");
    CHECK(vfs_create("/home/iker/readme.txt", NULL) == 0, "create /home/iker/readme.txt");

    vnode_t *iker = vfs_resolve("/home/iker", NULL);
    CHECK(iker != NULL, "ruta absoluta");
    CHECK(vfs_resolve("readme.txt", iker) != NULL, "ruta relativa: readme.txt desde /home/iker");
    CHECK(vfs_resolve("./readme.txt", iker) == vfs_resolve("readme.txt", iker), "'./' se ignora");
    CHECK(vfs_resolve("notas/..", iker) == iker, "'..' sube un nivel");
    CHECK(vfs_resolve("../..", iker) == vfs_root(), "'../..' llega a la raiz");
    CHECK(vfs_resolve("../../../../..", iker) == vfs_root(), "'..' en la raiz se queda en la raiz");
    CHECK(vfs_resolve("//home//iker//", NULL) == iker, "barras multiples y finales se toleran");
    CHECK(vfs_resolve("/home/iker/nada", NULL) == NULL, "ruta inexistente = NULL");
    CHECK(vfs_resolve("/home/iker/readme.txt/x", NULL) == NULL, "buscar dentro de un archivo = NULL");

    vnode_t *notas = vfs_resolve("/home/iker/notas", NULL);
    CHECK(vfs_path_of(notas, buf, sizeof(buf)) == 0 && strcmp(buf, "/home/iker/notas") == 0,
          "path_of reconstruye /home/iker/notas");
    CHECK(vfs_path_of(notas, buf, 16) == VFS_EINVAL, "path_of con buffer corto = EINVAL");
    CHECK(vfs_path_of(notas, buf, 17) == 0, "path_of con el tamano justo (17) cabe");
}

static void test_mkdir_errors(void) {
    fresh();
    char longname[48];
    memset(longname, 'a', 32);
    longname[32] = '\0';
    char ok31[48];
    memset(ok31, 'b', 31);
    ok31[31] = '\0';
    char path[64];

    CHECK(vfs_mkdir("/a/b", NULL) == VFS_ENOENT, "mkdir con padre inexistente = ENOENT");
    CHECK(vfs_mkdir("/a", NULL) == 0 && vfs_mkdir("/a", NULL) == VFS_EEXIST, "mkdir duplicado = EEXIST");
    snprintf(path, sizeof(path), "/%s", longname);
    CHECK(vfs_mkdir(path, NULL) == VFS_ENAMETOOLONG, "nombre de 32 caracteres = ENAMETOOLONG");
    snprintf(path, sizeof(path), "/%s", ok31);
    CHECK(vfs_mkdir(path, NULL) == 0, "nombre de 31 caracteres es valido");
    CHECK(vfs_mkdir("/", NULL) == VFS_EINVAL, "mkdir '/' = EINVAL");
    CHECK(vfs_mkdir("/a/..", NULL) == VFS_EINVAL && vfs_mkdir("/a/.", NULL) == VFS_EINVAL,
          "mkdir con '.' o '..' como nombre = EINVAL");
    vfs_create("/f", NULL);
    CHECK(vfs_mkdir("/f/x", NULL) == VFS_ENOTDIR, "mkdir bajo un archivo = ENOTDIR");
}

static void test_depth(void) {
    fresh();
    char path[VFS_PATH_MAX];
    uint32_t pos = 0;
    int ok = 1;
    for (int i = 1; i <= VFS_MAX_DEPTH; i++) {
        pos += (uint32_t)snprintf(path + pos, sizeof(path) - pos, "/d%d", i);
        if (vfs_mkdir(path, NULL) != 0) {
            ok = 0;
        }
    }
    CHECK(ok, "anidamiento de 16 niveles se crea");
    snprintf(path + pos, sizeof(path) - pos, "/extra");
    CHECK(vfs_mkdir(path, NULL) == VFS_ETOODEEP, "el nivel 17 se rechaza (ETOODEEP)");
    char buf[VFS_PATH_MAX];
    pos = 0;
    path[0] = '\0';
    for (int i = 1; i <= VFS_MAX_DEPTH; i++) {
        pos += (uint32_t)snprintf(path + pos, sizeof(path) - pos, "/d%d", i);
    }
    CHECK(vfs_path_of(vfs_resolve(path, NULL), buf, sizeof(buf)) == 0 && strcmp(buf, path) == 0,
          "path_of del nodo mas profundo coincide");
}

static void test_readdir_rmdir(void) {
    fresh();
    vfs_mkdir("/b", NULL);
    vfs_mkdir("/a", NULL);
    vfs_create("/c", NULL);
    CHECK(vfs_root()->size == 3, "el directorio cuenta sus hijos (3)");
    CHECK(strcmp(vfs_readdir(vfs_root(), 0)->name, "a") == 0 &&
          strcmp(vfs_readdir(vfs_root(), 1)->name, "b") == 0 &&
          strcmp(vfs_readdir(vfs_root(), 2)->name, "c") == 0, "readdir devuelve los hijos ordenados por nombre");
    CHECK(vfs_readdir(vfs_root(), 3) == NULL, "readdir fuera de rango = NULL");
    CHECK(vfs_readdir(vfs_resolve("/c", NULL), 0) == NULL, "readdir sobre un archivo = NULL");

    vfs_mkdir("/a/x", NULL);
    CHECK(vfs_rmdir("/a", NULL) == VFS_ENOTEMPTY, "rmdir de un directorio con hijos = ENOTEMPTY");
    CHECK(vfs_rmdir("/a/x", NULL) == 0 && vfs_rmdir("/a", NULL) == 0, "rmdir de directorios vacios funciona");
    CHECK(vfs_rmdir("/", NULL) == VFS_EBUSY, "no se puede borrar la raiz");
    CHECK(vfs_rmdir("/c", NULL) == VFS_ENOTDIR, "rmdir sobre un archivo = ENOTDIR");
    CHECK(vfs_rmdir("/zzz", NULL) == VFS_ENOENT, "rmdir de lo inexistente = ENOENT");
}

static void test_files(void) {
    fresh();
    heap_stats_t base = hs();
    vfs_create("/f", NULL);
    vnode_t *f = vfs_resolve("/f", NULL);
    char out[8200];

    CHECK(vfs_write(f, 0, "hola", 4) == 4 && f->size == 4, "write crece el archivo vacio");
    CHECK(vfs_read(f, 0, out, sizeof(out)) == 4 && memcmp(out, "hola", 4) == 0, "read devuelve lo escrito");
    CHECK(vfs_read(f, 4, out, 10) == 0, "read al final devuelve 0");
    CHECK(vfs_read(f, 1, out, 2) == 2 && memcmp(out, "ol", 2) == 0, "read parcial con offset");

    CHECK(vfs_write(f, 10, "X", 1) == 1 && f->size == 11, "write mas alla del final crece el archivo");
    vfs_read(f, 0, out, 11);
    int zeros = 1;
    for (int i = 4; i < 10; i++) {
        if (out[i] != 0) {
            zeros = 0;
        }
    }
    CHECK(zeros && out[10] == 'X', "el hueco entre el final y el offset es de ceros");

    char big[4096];
    for (int i = 0; i < 4096; i++) {
        big[i] = (char)(i * 7 + 3);
    }
    vfs_truncate(f);
    CHECK(f->size == 0, "truncate deja el tamano en 0");
    CHECK(vfs_write(f, 0, big, 4096) == 4096 && f->size == 4096, "archivo de 4 KB se escribe");
    CHECK(vfs_read(f, 0, out, 4096) == 4096 && memcmp(out, big, 4096) == 0, "y se lee integro y verificado");
    CHECK(vfs_write(f, 100, "ZZ", 2) == 2, "sobrescribir en el medio");
    vfs_read(f, 98, out, 6);
    CHECK(out[2] == 'Z' && out[3] == 'Z' && f->size == 4096, "sobrescribir no cambia el tamano");

    CHECK(vfs_unlink("/", NULL) == VFS_EISDIR, "unlink de un directorio = EISDIR");
    CHECK(vfs_unlink("/f", NULL) == 0 && vfs_resolve("/f", NULL) == NULL, "unlink borra el archivo");
    CHECK(same_heap(base, hs()), "tras borrar el archivo el heap vuelve al estado exacto");
}

static void test_enomem(void) {
    fresh();
    vfs_create("/f", NULL);
    vnode_t *f = vfs_resolve("/f", NULL);
    vfs_write(f, 0, "abcdefghij", 10);

    static void *hog[1000];
    int n = 0;
    while (n < 1000 && (hog[n] = kmalloc(256)) != NULL) {
        n++;
    }
    int m = n;
    while (m < 1000 && (hog[m] = kmalloc(16)) != NULL) {
        m++;
    }
    char big[2000];
    memset(big, 'q', sizeof(big));
    CHECK(vfs_write(f, 0, big, sizeof(big)) == VFS_ENOMEM, "write sin memoria devuelve ENOMEM");
    char out[16];
    CHECK(f->size == 10 && vfs_read(f, 0, out, 10) == 10 && memcmp(out, "abcdefghij", 10) == 0,
          "tras el fallo los datos existentes siguen intactos");
    CHECK(vfs_create("/g", NULL) == VFS_ENOMEM && vfs_resolve("/g", NULL) == NULL,
          "crear un nodo sin memoria devuelve ENOMEM y no deja nada a medias");
    for (int i = 0; i < m; i++) {
        kfree(hog[i]);
    }
    CHECK(vfs_write(f, 0, big, sizeof(big)) == (int)sizeof(big), "liberada la memoria el write funciona");
}

static void test_descriptors(void) {
    fresh();
    heap_stats_t base = hs();
    char out[64];

    CHECK(vfs_open("/nada", VFS_O_RDONLY, NULL) == VFS_ENOENT, "open de un archivo inexistente = ENOENT");
    int fd = vfs_open("/a.txt", VFS_O_WRONLY | VFS_O_CREAT, NULL);
    CHECK(fd == 0, "open con O_CREAT crea el archivo y devuelve el fd 0");
    CHECK(vfs_write_fd(fd, "hola mundo", 10) == 10, "write_fd escribe");
    CHECK(vfs_read_fd(fd, out, 4) == VFS_EBADF, "leer un fd de solo escritura = EBADF");
    CHECK(vfs_unlink("/a.txt", NULL) == VFS_EBUSY, "no se borra un archivo abierto (EBUSY)");
    CHECK(vfs_close(fd) == 0 && vfs_close(fd) == VFS_EBADF, "close ok; cerrar dos veces = EBADF");

    fd = vfs_open("/a.txt", VFS_O_RDONLY, NULL);
    CHECK(vfs_read_fd(fd, out, 4) == 4 && memcmp(out, "hola", 4) == 0, "read_fd lee desde el inicio");
    CHECK(vfs_read_fd(fd, out, 100) == 6, "y continua donde quedo");
    CHECK(vfs_write_fd(fd, "x", 1) == VFS_EBADF, "escribir un fd de solo lectura = EBADF");
    CHECK(vfs_seek(fd, 0, VFS_SEEK_SET) == 0 && vfs_read_fd(fd, out, 2) == 2 && memcmp(out, "ho", 2) == 0,
          "seek SET 0 y relee");
    CHECK(vfs_seek(fd, -3, VFS_SEEK_END) == 7, "seek END -3 = 7");
    CHECK(vfs_seek(fd, 1, VFS_SEEK_CUR) == 8, "seek CUR +1");
    CHECK(vfs_seek(fd, -100, VFS_SEEK_SET) == VFS_EINVAL, "seek a una posicion negativa = EINVAL");
    vfs_close(fd);

    fd = vfs_open("/a.txt", VFS_O_WRONLY | VFS_O_APPEND, NULL);
    vfs_write_fd(fd, "!!", 2);
    vfs_close(fd);
    fd = vfs_open("/a.txt", VFS_O_RDONLY, NULL);
    CHECK(vfs_read_fd(fd, out, 64) == 12 && memcmp(out, "hola mundo!!", 12) == 0, "O_APPEND agrega al final");
    vfs_close(fd);
    fd = vfs_open("/a.txt", VFS_O_WRONLY | VFS_O_TRUNC, NULL);
    CHECK(vfs_resolve("/a.txt", NULL)->size == 0, "O_TRUNC vacia el archivo");
    vfs_close(fd);

    int fds[TASK_MAX_FDS + 1];
    int got = 0;
    for (int i = 0; i < TASK_MAX_FDS; i++) {
        fds[i] = vfs_open("/a.txt", VFS_O_RDONLY, NULL);
        if (fds[i] >= 0) {
            got++;
        }
    }
    CHECK(got == TASK_MAX_FDS && vfs_open("/a.txt", VFS_O_RDONLY, NULL) == VFS_ENFILE,
          "8 descriptores por tarea; el noveno = ENFILE");
    for (int i = 0; i < TASK_MAX_FDS; i++) {
        vfs_close(fds[i]);
    }
    CHECK(vfs_read_fd(99, out, 1) == VFS_EBADF && vfs_close(-1) == VFS_EBADF,
          "descriptor invalido devuelve error, no corrupcion");
    CHECK(vfs_open("/", VFS_O_RDONLY, NULL) == VFS_EISDIR, "open de un directorio = EISDIR");

    vfs_unlink("/a.txt", NULL);
    CHECK(same_heap(base, hs()), "tras cerrar y borrar, el heap vuelve al estado exacto");
}

static uint32_t gen_abc(char *buf, uint32_t cap) {
    (void)cap;
    memcpy(buf, "abcdef", 6);
    return 6;
}

static void test_virtual(void) {
    fresh();
    vfs_mkdir("/proc", NULL);
    CHECK(vfs_create_virtual("/proc/x", NULL, gen_abc) == 0, "create_virtual crea el archivo");
    vnode_t *x = vfs_resolve("/proc/x", NULL);
    char out[16];
    CHECK(x->size == 0 && x->perms == VFS_PERM_R, "un virtual no almacena nada (tamano 0) y es de solo lectura");
    CHECK(vfs_read(x, 0, out, 16) == 6 && memcmp(out, "abcdef", 6) == 0, "se genera al leer");
    CHECK(vfs_read(x, 4, out, 16) == 2 && memcmp(out, "ef", 2) == 0, "lectura con offset sobre el contenido generado");
    CHECK(vfs_read(x, 6, out, 16) == 0, "fin del contenido generado = 0");
    CHECK(vfs_write(x, 0, "z", 1) == VFS_EINVAL, "no se puede escribir un virtual");
    CHECK(vfs_open("/proc/x", VFS_O_WRONLY, NULL) == VFS_EINVAL, "ni abrirlo para escribir");
    int fd = vfs_open("/proc/x", VFS_O_RDONLY, NULL);
    CHECK(fd >= 0 && vfs_read_fd(fd, out, 16) == 6, "pero si abrirlo para leer");
    vfs_close(fd);
}

static void test_rm_recursive(void) {
    fresh();
    heap_stats_t base = hs();
    vfs_mkdir("/t", NULL);
    vfs_mkdir("/t/a", NULL);
    vfs_mkdir("/t/a/b", NULL);
    vfs_create("/t/a/b/f1", NULL);
    vfs_create("/t/a/f2", NULL);
    vfs_create("/t/f3", NULL);
    vfs_write(vfs_resolve("/t/a/b/f1", NULL), 0, "datos", 5);
    vfs_write(vfs_resolve("/t/f3", NULL), 0, "mas datos", 9);

    vnode_t *deep = vfs_resolve("/t/a/b", NULL);
    vfs_hold(deep);                                   /* como si fuera el cwd de una tarea */
    CHECK(vfs_rm_recursive("/t", NULL) == VFS_EBUSY && vfs_resolve("/t/a/b/f1", NULL) != NULL,
          "rm -r se niega si algo dentro esta en uso y no borra nada");
    CHECK(vfs_rmdir("/t/a/b", NULL) == VFS_ENOTEMPTY, "rmdir normal sigue exigiendo directorio vacio");
    vfs_release(deep);
    CHECK(vfs_rm_recursive("/t", NULL) == 0 && vfs_resolve("/t", NULL) == NULL, "rm -r borra todo el subarbol");
    CHECK(same_heap(base, hs()), "sin fugas: el heap vuelve al estado exacto tras rm -r");
    CHECK(vfs_rm_recursive("/", NULL) == VFS_EBUSY, "rm -r de la raiz = EBUSY");
}

static void test_leaks(void) {
    fresh();
    heap_stats_t base = hs();
    char path[48];
    for (int i = 0; i < 100; i++) {
        snprintf(path, sizeof(path), "/n%d", i);
        if (i % 3 == 0) {
            vfs_mkdir(path, NULL);
        } else {
            vfs_create(path, NULL);
            vfs_write(vfs_resolve(path, NULL), 0, "contenido de prueba", 19);
        }
    }
    CHECK(vfs_root()->size == 100, "100 nodos creados");
    int ok = 1;
    for (int i = 0; i < 100; i++) {
        snprintf(path, sizeof(path), "/n%d", i);
        int rc = (i % 3 == 0) ? vfs_rmdir(path, NULL) : vfs_unlink(path, NULL);
        if (rc != 0) {
            ok = 0;
        }
    }
    CHECK(ok && vfs_root()->size == 0, "100 nodos borrados");
    CHECK(same_heap(base, hs()), "crear y borrar 100 nodos deja el heap en su estado inicial exacto");
}

int main(void) {
    test_root_and_paths();
    test_mkdir_errors();
    test_depth();
    test_readdir_rmdir();
    test_files();
    test_enomem();
    test_descriptors();
    test_virtual();
    test_rm_recursive();
    test_leaks();

    printf("\n%d asserts, %d fallos -> %s\n", g_total, g_failed, g_failed ? "FALLO" : "OK");
    return g_failed ? 1 : 0;
}
