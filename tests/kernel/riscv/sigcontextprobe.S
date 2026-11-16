.section .rodata
.option norvc
.balign 4
.global proc_begin,proc_end,proc_call
proc_begin:
 li t3,1
 li t0,0x3fff000000
 bne a0,t0,1f
 ld t1,0(a0)
 li t2,0x4b424f4f54303031
 bne t1,t2,1f
 li t3,2
 li t0,0x12345678
 bne s1,t0,1f
 li t3,3
 fmv.x.d t0,f31
 li t1,0x5678
 bne t0,t1,1f
 li t3,4
 frcsr t0
 bne t0,a6,1f
 li a0,42
 li a1,0
 li a2,0
 li a3,0
 li a4,0
 li a5,0
 li a6,0
 li a7,1
proc_call:
 ecall
1:
 addi a0,t3,180
 li a1,0
 li a2,0
 li a3,0
 li a4,0
 li a5,0
 li a6,0
 li a7,1
 ecall
 .word 0
proc_end:
