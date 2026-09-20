    .section .text
    .align  4
    .global _start

_start:
    .literal_position
    movi    a1, _stack_top     /* a1 es el stack pointer en Xtensa */
    call0   kmain              /* llamada absoluta, no requiere ventana de registros */

1:
    j       1b                 /* si kmain retorna (no debería), quedar aquí */
