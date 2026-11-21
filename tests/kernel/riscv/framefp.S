.section .text
.option norvc
.balign 4
.global frame_fp
.type frame_fp,@function
frame_fp:
 fsrm a0,a0
 ret
.size frame_fp,.-frame_fp
.section .note.GNU-stack,"",@progbits
#include <abi/syscall.h>
#include <abi/base.h>
#include <abi/boot.inc>
.section .text
.balign 4
.global exec_entry, exec_end
exec_entry:
 li t1,0x30000000
 bne a0,t1,3f
 ld s1,BOOT_HEADER_BYTES+8(a0)
1:
 mv a0,s1
 li a7,SYS_EXECPREP
 ecall
 beqz a0,2f
 li t1,-ERR_BUSY
 bne a0,t1,3f
 li a0,0
 li a7,SYS_YIELD
 ecall
 j 1b
2:
 mv a0,a1
 li a1,0
 li a2,0
 li a7,SYS_EXECABORT
 ecall
 bnez a0,3f
 li a0,42
 j 4f
3:
 li a0,93
4:
 li a7,SYS_EXIT
 ecall
 ebreak
exec_end:
.balign 4
.global frame_wait
.type frame_wait,@function
frame_wait:
 addi sp,sp,-16
 sd s0,0(sp)
 mv s0,sp
 li sp,0x3ffffef040
 li a0,0
 li a1,0
 li a2,0
 li a3,0
 li a4,0
 li a5,0
 li a6,0
 li a7,SYS_YIELD
 ecall
 mv sp,s0
 ld s0,0(sp)
 addi sp,sp,16
 ret
.size frame_wait,.-frame_wait
