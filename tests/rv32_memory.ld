OUTPUT_ARCH(riscv)
ENTRY(_start)
SECTIONS {
    . = 0x1000;
    .text : { *(.text.start) *(.text*) *(.rodata*) *(.srodata*) }
    . = 0x80000000;
    .data : { *(.data*) *(.sdata*) }
    PROVIDE(__global_pointer$ = 0x80000800);
    . = 0x80000800;
    .results (NOLOAD) : { *(.bss.output) }
    .bss (NOLOAD) : { *(.bss*) *(.sbss*) }
    ASSERT(SIZEOF(.text) <= 4096, "probe text exceeds local program memory")
    ASSERT(SIZEOF(.data) <= 2048, "probe data overlaps results")
    ASSERT(SIZEOF(.results) == 72, "probe result layout changed")
    ASSERT(. <= 0x80001000, "probe globals exceed system fixture")
}
