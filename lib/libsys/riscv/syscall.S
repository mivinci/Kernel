#include <abi/base.h>
#include <abi/syscall.h>
.section .text
.balign 4
.global __sys_enter
.type __sys_enter, @function
__sys_enter:
    addi sp, sp, -16
    sd s0, 0(sp)
    mv s0, a2
    mv t1, a1
    mv a7, a0
    ld a0, 0(t1)
    ld a1, 8(t1)
    ld a2, 16(t1)
    ld a3, 24(t1)
    ld a4, 32(t1)
    ld a5, 40(t1)
    ld a6, 48(t1)
    ecall
    sd a1, 0(s0)
    sd a2, 8(s0)
    sd a3, 16(s0)
    sd a4, 24(s0)
    sd a5, 32(s0)
    sd a6, 40(s0)
    sd a7, 48(s0)
    sd t0, 56(s0)
    ld s0, 0(sp)
    addi sp, sp, 16
    ret
.size __sys_enter, .-__sys_enter

.balign 4
.global __sys_forkprep
.type __sys_forkprep, @function
__sys_forkprep:
    beqz a2, 1f
    addi sp, sp, -16
    sd s0, 0(sp)
    mv s0, a2
    li a2, 0
    li a3, 0
    li a4, 0
    li a5, 0
    li a6, 0
    li a7, SYS_FORKPREP
    ecall
.global __sys_forkreturn_begin
__sys_forkreturn_begin:
    sd a1, 0(s0)
    sd a2, 8(s0)
    sd a3, 16(s0)
.global __sys_forkreturn_end
__sys_forkreturn_end:
    ld s0, 0(sp)
    addi sp, sp, 16
    ret
1:
    li a0, -ERR_INVALID
    ret
.size __sys_forkprep, .-__sys_forkprep
.section .note.GNU-stack,"",@progbits
