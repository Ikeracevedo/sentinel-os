#include <sentinel/kprintf.h>
#include <sentinel/panic.h>
#include <stdint.h>

/* EPC_1=177 y EXCVADDR=238: registros especiales numerados
 * (xtensa/config/specreg.h de Espressif). El ensamblador GNU no reconoce
 * el nombre simbolico dentro de un string de C -- solo dentro de .S
 * preprocesados. */
void panic(const char *msg) {
    uint32_t ps;
    __asm__ __volatile__("rsr %0, PS" : "=r"(ps));
    kprintf("PANIC: %s\n  PS=%p\n", msg, (void *)ps);
    for (;;) {
    }
}

void panic_unexpected_exception(void) {
    uint32_t cause, epc, vaddr, ps;
    __asm__ __volatile__("rsr %0, EXCCAUSE" : "=r"(cause));
    __asm__ __volatile__("rsr %0, 177" : "=r"(epc));      /* EPC_1 */
    __asm__ __volatile__("rsr %0, 238" : "=r"(vaddr));    /* EXCVADDR */
    __asm__ __volatile__("rsr %0, PS" : "=r"(ps));

    kprintf("PANIC: excepcion inesperada\n");
    kprintf("  EXCCAUSE=%u  EPC1=%p  EXCVADDR=%p  PS=%p\n", cause, (void *)epc, (void *)vaddr, (void *)ps);
    for (;;) {
    }
}
