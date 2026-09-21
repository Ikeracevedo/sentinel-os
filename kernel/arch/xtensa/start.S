    .section .text
    .align  4
    .global _start

_start:
    .literal_position

    /* Desactivar los 3 watchdogs heredados del ROM bootloader (R2.2) */

    /* RTC Watchdog */
    movi    a2, 0x3ff480a4
    movi    a3, 0x50d83aa1
    s32i    a3, a2, 0
    movi    a2, 0x3ff4808c
    movi    a3, 0
    s32i    a3, a2, 0

    /* Timer Group 0 Watchdog */
    movi    a2, 0x3ff5f064
    movi    a3, 0x50d83aa1
    s32i    a3, a2, 0
    movi    a2, 0x3ff5f048
    movi    a3, 0
    s32i    a3, a2, 0

    /* Timer Group 1 Watchdog */
    movi    a2, 0x3ff60064
    movi    a3, 0x50d83aa1
    s32i    a3, a2, 0
    movi    a2, 0x3ff60048
    movi    a3, 0
    s32i    a3, a2, 0

    movi    a1, _stack_top     /* a1 = stack pointer */

    /* limpiar .bss: desde _bss_start hasta _bss_end, de 4 en 4 bytes */
    movi    a2, _bss_start
    movi    a3, _bss_end
    movi    a4, 0

bss_clear_loop:
    bge     a2, a3, bss_clear_done
    s32i    a4, a2, 0
    addi    a2, a2, 4
    j       bss_clear_loop

bss_clear_done:
    call0   kmain

halt:
    j       halt
