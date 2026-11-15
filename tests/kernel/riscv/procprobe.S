.section .rodata
.option norvc
.balign 4
.global proc_begin, proc_end, proc_call
proc_begin:
 li t0, 0x3fff000000
 bne a0, t0, 1f
 ld t1, 0(a0)
 li t2, 0x4b424f4f54303031
 bne t1, t2, 1f
 li a0, 42
 li a7, 1
proc_call:
 ecall
1: .word 0
proc_end:
