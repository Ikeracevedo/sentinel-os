MEMORY
{
    iram (rwx) : ORIGIN = 0x40080000, LENGTH = 128K
    dram (rwx) : ORIGIN = 0x3FFB0000, LENGTH = 176K
}

ENTRY(_start)

SECTIONS
{
    .vectors : ALIGN(1024)
    {
        *(.VectorTable.text)
    } > iram

    .text :
    {
        *(.literal) *(.literal.*)
        *(.text) *(.text.*)
    } > iram

    .rodata :
    {
        *(.rodata) *(.rodata.*)
    } > dram

    .data : { *(.data*) } > dram

    .bss :
    {
        . = ALIGN(4);
        _bss_start = .;
        *(.bss*) *(COMMON)
        . = ALIGN(4);
        _bss_end = .;
    } > dram

    . = ALIGN(4);
    _heap_start = .;
    _heap_end = ORIGIN(dram) + LENGTH(dram) - 0x1000;
    _stack_top = ORIGIN(dram) + LENGTH(dram);
}
