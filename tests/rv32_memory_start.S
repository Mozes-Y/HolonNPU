.option norvc
.option norelax
.section .text.start
.global _start
_start:
    li sp, 0x10001000
    la gp, __global_pointer$
    call probe
    fence iorw, iorw
1:  wfi
    j 1b
