#include <abi/syscall.h>
.section .rodata
.balign 4
.global fork_code_begin,fork_code_end
fork_code_begin:
.option push
.option norvc
 ecall
 addi s2,s2,1
 bnez a0,1f
 bnez a1,1f
 bne a2,s11,1f
 bne a3,s8,1f
 bnez a4,1f
 bnez a5,1f
 bnez a6,1f
 bnez a7,1f
 bnez t0,1f
 li t6,0x1013
 bne s2,t6,1f
 li t6,0x1001
 bne ra,t6,1f
 li t6,0x1003
 bne gp,t6,1f
 li t6,0x1004
 bne tp,t6,1f
 li t6,0x1006
 bne t1,t6,1f
 li t6,0x1007
 bne t2,t6,1f
 li t6,0x101c
 bne t3,t6,1f
 li t6,0x101d
 bne t4,t6,1f
 li t6,0x101e
 bne t5,t6,1f
 li t6,0x1008
 bne s0,t6,1f
 li t6,0x1009
 bne s1,t6,1f
 li t6,0x1013
 bne s3,t6,1f
 li t6,0x1019
 bne s9,t6,1f
 li t6,0x101a
 bne s10,t6,1f
 fmv.x.d t6,f31
 li t5,0x5678
 bne t6,t5,1f
 frcsr t6
 li t5,0x9f
 bne t6,t5,1f
 beqz s8,2f
 ld t6,0(s8)
 li t5,0x4711
 bne t6,t5,1f
2:
 li a0,0
 li a1,0
 li a2,0
 li a3,0
 li a4,0
 li a5,0
 li a6,0
 li a7,SYS_GETID
 ecall
 bnez a0,1f
 bne a1,s11,1f
 li t6,1
 bne a2,t6,1f
 bne a3,t6,1f
 li a0,42
 j 3f
1:
 li a0,210
3:
 li a1,0
 li a2,0
 li a3,0
 li a4,0
 li a5,0
 li a6,0
 li a7,SYS_EXIT
 ecall
 .word 0
.option pop
fork_code_end:
.section .note.GNU-stack,"",@progbits
