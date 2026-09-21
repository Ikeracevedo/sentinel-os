#include <sentinel/kprintf.h>
#include <stdint.h>

void panic_unexpected_exception(void) {
    uint32_t cause, epc;
    __asm__ __volatile__("rsr %0, EXCCAUSE" : "=r"(cause));
    /* EPC_1 = registro especial numero 177 (xtensa/config/specreg.h de
     * Espressif). El ensamblador de GNU no reconoce el nombre simbolico
     * dentro de un string de C -- solo funciona en archivos .S procesados
     * por el preprocesador, que expande la macro antes de ensamblar. */
    __asm__ __volatile__("rsr %0, 177" : "=r"(epc));
    kprintf("PANIC: excepcion inesperada -- cause=%u epc1=%p\n", cause, (void *)epc);
    for (;;) {
    }
}
