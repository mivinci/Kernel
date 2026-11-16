#include <abi/syscall.h>
.section .rodata
.balign 8
.option norvc
.global irq_begin, irq_end
irq_begin:
 ld t0, 72(a0)
 add t0, t0, a0
 ld s0, 40(t0)
 addi sp, sp, -64
 li t0, 65536
 sd t0, 0(sp)
 li t0, 1
 sd t0, 8(sp)
 sd zero, 16(sp)
 sd zero, 24(sp)
 sd zero, 32(sp)
 sd zero, 40(sp)
 li s1, 4096
irq_loop:
 mv a0, s0
 mv a1, sp
 li a2, 0
 li a3, 0
 li a4, 0
 li a5, 0
 li a6, 0
 li a7, SYS_ALLOC
 ecall
 bnez a0, irq_bad
 mv a0, a1
 li a1, 0
 li a7, SYS_FREE
 ecall
 bnez a0, irq_bad
 addi s1, s1, -1
 bnez s1, irq_loop
 li a0, 42
 j irq_exit
irq_bad:
 li a0, 17
irq_exit:
 li a1, 0
 li a2, 0
 li a3, 0
 li a4, 0
 li a5, 0
 li a6, 0
 li a7, SYS_EXIT
 ecall
 j irq_bad
irq_end:
