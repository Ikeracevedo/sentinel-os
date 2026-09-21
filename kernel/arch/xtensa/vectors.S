/* EXCSAVE_1 = registro especial 209 (xtensa/config/specreg.h de Espressif).
 * No es un nombre nativo del ensamblador GNU -- solo funciona si se
 * incluye ese header, o si se define aqui como macro de C (este archivo
 * .S SI pasa por el preprocesador, a diferencia de un string en un .c). */
#define EXCSAVE_1 209

    .section .VectorTable.text, "ax"
    .align  1024
    .global _vector_table
_vector_table:

    .org 0x000
    j .                 /* WindowOverflow4  -- no usado (call0 ABI) */
    .org 0x040
    j .                 /* WindowUnderflow4 -- no usado */
    .org 0x080
    j .                 /* WindowOverflow8  -- no usado */
    .org 0x0C0
    j .                 /* WindowUnderflow8 -- no usado */
    .org 0x100
    j .                 /* WindowOverflow12 -- no usado */
    .org 0x140
    j .                 /* WindowUnderflow12 -- no usado */
    .org 0x180
    j .                 /* Interrupcion nivel 2 -- no configurada */
    .org 0x1C0
    j .                 /* Interrupcion nivel 3 -- no configurada */
    .org 0x200
    j .                 /* Interrupcion nivel 4 -- no configurada */
    .org 0x240
    j .                 /* Interrupcion nivel 5 -- no configurada */
    .org 0x280
    j .                 /* Interrupcion nivel 6 / debug -- no configurada */
    .org 0x2C0
    j .                 /* NMI -- no configurada */
    .org 0x300
    j .                 /* Excepcion de kernel -- no usada (siempre en modo usuario) */

    .org 0x340
    .global _UserExceptionVector
_UserExceptionVector:
    wsr     a0, EXCSAVE_1          /* guardar a0 para tener un registro libre */
    rsr     a0, EXCCAUSE           /* a0 = motivo de la excepcion */
    beqi    a0, 4, .Lis_interrupt  /* 4 = EXCCAUSE_LEVEL1_INTERRUPT */

    /* No es una interrupcion -- es una excepcion real (ej. LoadStoreError).
     * Manejo completo llega en HU-E01-09; por ahora, panico minimo. */
    j       panic_unexpected_exception

.Lis_interrupt:
    /* Guardar el contexto minimo en la pila actual antes de tocar nada */
    addi    a1, a1, -64
    s32i    a2,  a1, 8
    s32i    a3,  a1, 12
    s32i    a4,  a1, 16
    s32i    a5,  a1, 20
    s32i    a6,  a1, 24
    s32i    a7,  a1, 28
    s32i    a8,  a1, 32
    s32i    a9,  a1, 36
    s32i    a10, a1, 40
    s32i    a11, a1, 44
    s32i    a12, a1, 48
    s32i    a13, a1, 52
    s32i    a14, a1, 56
    s32i    a15, a1, 60
    rsr     a2, EXCSAVE_1          /* recuperar el a0 original */
    s32i    a2,  a1, 4

    call0   irq_dispatch           /* aqui se decide que interrupcion fue */

    l32i    a2,  a1, 4
    wsr     a2, EXCSAVE_1
    l32i    a2,  a1, 8
    l32i    a3,  a1, 12
    l32i    a4,  a1, 16
    l32i    a5,  a1, 20
    l32i    a6,  a1, 24
    l32i    a7,  a1, 28
    l32i    a8,  a1, 32
    l32i    a9,  a1, 36
    l32i    a10, a1, 40
    l32i    a11, a1, 44
    l32i    a12, a1, 48
    l32i    a13, a1, 52
    l32i    a14, a1, 56
    l32i    a15, a1, 60
    addi    a1, a1, 64
    rsr     a0, EXCSAVE_1
    rfe                             /* Return From Exception (nivel 1) */

    .org 0x3C0
    j .                 /* Doble excepcion -- no debería pasar nunca */
