MEMORY
{
    iram (rwx) : ORIGIN = 0x40080000, LENGTH = 128K
    dram (rwx) : ORIGIN = 0x3FFB0000, LENGTH = 176K
}

ENTRY(_start)

_stack_top = ORIGIN(dram) + LENGTH(dram);

SECTIONS
{
    .text :
    {
        *(.literal) *(.literal.*)
        *(.text) *(.text.*)
    } > iram

    .rodata : { *(.rodata*) } > iram
    .data   : { *(.data*) }   > dram
    .bss    : { *(.bss*) *(COMMON) } > dram
}
