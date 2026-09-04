    .section .text.init
    .globl _start
_start:
    la sp, _stack_top
    call main
    beqz a0, 1f
    mv a1, a0
    li a0, 0
    .word 0x4400007b
0:
    j 0b
1:
    li a0, 0
    .word 0x4200007b
2:
    j 2b
