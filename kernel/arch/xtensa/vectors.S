/* EXCSAVE_1 = registro especial 209 (xtensa/config/specreg.h de Espressif).
 * No es un nombre nativo del ensamblador GNU -- solo funciona si se
 * incluye ese header, o si se define aqui como macro de C (este archivo
 * .S SI pasa por el preprocesador, a diferencia de un string en un .c). */
#define EXCSAVE_1 209
#define EPC_1 177

    .section .VectorTable.text, "ax"
    .align  1024
    .global _vector_table
_vector_table:

        .org 0x000
    _WindowOverflow4:
        s32e    a0, a5, -16
        s32e    a1, a5, -12
        s32e    a2, a5, -8
        s32e    a3, a5, -4
        rfwo

        .org 0x040
    _WindowUnderflow4:
        l32e    a0, a5, -16
        l32e    a1, a5, -12
        l32e    a2, a5, -8
        l32e    a3, a5, -4
        rfwu

        .org 0x080
    _WindowOverflow8:
        s32e    a0, a9, -16
        l32e    a0, a1, -12
        s32e    a1, a9, -12
        s32e    a2, a9, -8
        s32e    a3, a9, -4
        s32e    a4, a0, -32
        s32e    a5, a0, -28
        s32e    a6, a0, -24
        s32e    a7, a0, -20
        rfwo

        .org 0x0C0
    _WindowUnderflow8:
        l32e    a0, a9, -16
        l32e    a1, a9, -12
        l32e    a2, a9, -8
        l32e    a7, a1, -12
        l32e    a3, a9, -4
        l32e    a4, a7, -32
        l32e    a5, a7, -28
        l32e    a6, a7, -24
        l32e    a7, a7, -20
        rfwu

        .org 0x100
        j .                 /* WindowOverflow12  -- no usado por ahora, ver nota */
        .org 0x140
        j .                 /* WindowUnderflow12 -- no usado por ahora, ver nota */

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
    addi    a1, a1, -96
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
    rsr     a2, EXCSAVE_1          /* a0 original */
    s32i    a2,  a1, 4
    rsr     a2, ps                 /* PS del interrumpido (trae EXCM=1) */
    s32i    a2,  a1, 64
    rsr     a2, EPC_1              /* PC al que hay que volver */
    s32i    a2,  a1, 68

    /* PS para C: WOE | UM | INTLEVEL(1), EXCM=0 = 0x00040021.
     * Se arma con shifts porque movi de 32 bits genera un literal y este
     * archivo va antes de .text en el linker. */
    movi    a2, 0x40
    slli    a2, a2, 12
    addi    a2, a2, 0x21
    wsr     a2, ps
    rsync

    call4   irq_dispatch

    l32i    a2,  a1, 64            /* PS original: restaura EXCM=1 e INTLEVEL */
    wsr     a2, ps
    rsync
    l32i    a2,  a1, 68
    wsr     a2, EPC_1
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
    addi    a1, a1, 96
    rsr     a0, EXCSAVE_1
    rfe