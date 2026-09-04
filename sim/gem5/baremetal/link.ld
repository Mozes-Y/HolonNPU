OUTPUT_ARCH(riscv)
ENTRY(_start)

PHDRS
{
    text PT_LOAD FLAGS(5);
    data PT_LOAD FLAGS(6);
}

SECTIONS
{
    . = 0x80000000;
    .text : { *(.text.init) *(.text*) } :text
    .rodata : { *(.rodata*) } :text
    . = ALIGN(0x1000);
    .data : { *(.data*) } :data
    .bss : { *(.bss*) *(COMMON) } :data

    /* Page-tail placement makes the fast gate exercise every loader/writeback split. */
    . = 0x80010ff0;
    .holon_descriptor : { KEEP(*(.holon_descriptor)) } :data
    . = 0x80012ff0;
    .holon_program : { KEEP(*(.holon_program)) } :data
    . = 0x80013ff0;
    .holon_arguments : { KEEP(*(.holon_arguments)) } :data
    . = 0x80014ff0;
    .holon_completion : { KEEP(*(.holon_completion)) } :data

    . = 0x80016000;
    . += 0x4000;
    _stack_top = .;
}
