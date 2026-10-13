MEMORY
{
    iram (rwx) : ORIGIN = 0x40080000, LENGTH = 128K
    dram (rwx) : ORIGIN = 0x3FFB0000, LENGTH = 176K
    /* SRAM1 (datos, accesible por bytes). El ROM reserva 0x3FFE0000-0x3FFE0440 y
     * 0x3FFE3F20-0x3FFE4350 (heap/port/esp32/memory_layout.c de ESP-IDF): se empieza
     * por encima. Termina en 0x40000000. Aqui vive la arena del heap (ADR-017). */
    dram1 (rw) : ORIGIN = 0x3FFE5000, LENGTH = 108K
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

    /* Sin contenido en la imagen (NOLOAD): no se limpia ni se carga; el heap la inicializa. */
    .dram1 (NOLOAD) :
    {
        . = ALIGN(16);
        *(.dram1 .dram1.*)
    } > dram1

    . = ALIGN(4);
    _heap_start = .;
    _heap_end = ORIGIN(dram) + LENGTH(dram) - 0x1000;
    _stack_top = ORIGIN(dram) + LENGTH(dram);
}
