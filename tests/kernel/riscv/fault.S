#include <abi/riscv/trap.h>
#include <abi/signal.h>
.section .text
.option norvc
.balign 4
.global faulttrigger,fault_read,fault_write,fault_bad,fault_resume,faultget,faultsetpc
faulttrigger:
 addi sp,sp,-16
 sd ra,0(sp)
 li t0,1
 beq a0,t0,.Lwrite
 li t0,2
 beq a0,t0,.Lexec
 li t0,3
 beq a0,t0,fault_bad
 li t0,4
 beq a0,t0,fault_bad
 li t0,8
 bne a0,t0,.Lread
 li sp,0x3ffffe0000
.Lread:
 li t0,0x400000
fault_read:
 ld t1,0(t0)
 j fault_resume
.Lwrite:
 la t0,fault_readonly
fault_write:
 sd zero,0(t0)
 j fault_resume
.Lexec:
 la t0,fault_data
 jr t0
fault_bad:
 .word 0xffffffff
fault_resume:
 ld ra,0(sp)
 addi sp,sp,16
 ret
faultget:
 ld t0,TF_PC(a0)
 sd t0,0(a1)
 ld t0,TF_CAUSE(a0)
 sd t0,8(a1)
 ld t0,TF_ADDRESS(a0)
 sd t0,16(a1)
 ret
faultsetpc:
 sd a1,TF_PC(a0)
 ret
.section .rodata
.balign 8
.global fault_readonly,fault_alignment_pc,fault_alignment_signal,fault_alignment_reason
fault_readonly: .quad 1
fault_alignment_pc: .quad fault_bad
fault_alignment_signal: .quad SIGNAL_ILL
fault_alignment_reason: .quad SIGNAL_FAULT_ILLEGAL
.section .note.GNU-stack,"",@progbits
