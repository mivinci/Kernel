#include <abi/syscall.h>
#include <abi/ipc.h>
.option norvc
.section .rodata
.balign 8
.global group_begin, group_end, group_descendant
group_begin:
 li t0, 262144
 ld s0, 0(t0)
 li t0, 262144
 ld s1, 8(t0)
 li t0, 262144
 ld s2, 16(t0)
 li t0, 262144
 ld s3, 24(t0)
 li t0, 262144
 ld s4, 32(t0)
 li t0, 262144
 ld s5, 40(t0)
 li t0, 262144
 ld s6, 48(t0)
 li t0, 4660
 fmv.d.x f31, t0
 li s9, 1
 li a0, 0
 li a1, 0
 li a2, 0
 li a3, 0
 li a4, 0
 li a5, 0
 li a6, 0
 li a7, SYS_GETPG
 ecall
 bnez a0, group_bad
 bnez t0, group_bad
 li t1, 1
 bne a1, t1, group_bad
 li t1, 1
 bne a2, t1, group_bad
 bnez a3, group_bad
 bnez a4, group_bad
 bnez a5, group_bad
 bnez a6, group_bad
 bnez a7, group_bad
 li s9, 2
 li a0, 0
 li a1, 0
 li a2, 0
 li a3, 0
 li a4, 0
 li a5, 0
 li a6, 0
 mv a0, s2
 li a7, SYS_GETPG
 ecall
 bnez a0, group_bad
 bnez t0, group_bad
 li t1, 1
 bne a1, t1, group_bad
 li t1, 1
 bne a2, t1, group_bad
 bnez a3, group_bad
 bnez a4, group_bad
 bnez a5, group_bad
 bnez a6, group_bad
 bnez a7, group_bad
 li s9, 3
 li a0, 0
 li a1, 0
 li a2, 0
 li a3, 0
 li a4, 0
 li a5, 0
 li a6, 0
 mv a0, s0
 li a7, SYS_GETPG
 ecall
 bnez a0, group_bad
 bnez t0, group_bad
 li t1, 1
 bne a1, t1, group_bad
 li t1, 1
 bne a2, t1, group_bad
 bnez a3, group_bad
 bnez a4, group_bad
 bnez a5, group_bad
 bnez a6, group_bad
 bnez a7, group_bad
 li s9, 4
 li a0, 0
 li a1, 0
 li a2, 0
 li a3, 0
 li a4, 0
 li a5, 0
 li a6, 0
 li a0, -1
 li a7, SYS_GETPG
 ecall
 li t1, -1
 bne a0, t1, group_bad
 bnez t0, group_bad
 bnez a1, group_bad
 bnez a2, group_bad
 bnez a3, group_bad
 bnez a4, group_bad
 bnez a5, group_bad
 bnez a6, group_bad
 bnez a7, group_bad
 li s9, 5
 li a0, 0
 li a1, 0
 li a2, 0
 li a3, 0
 li a4, 0
 li a5, 0
 li a6, 0
 li a0, 576460752303423488
 li a7, SYS_GETPG
 ecall
 li t1, -3
 bne a0, t1, group_bad
 bnez t0, group_bad
 bnez a1, group_bad
 bnez a2, group_bad
 bnez a3, group_bad
 bnez a4, group_bad
 bnez a5, group_bad
 bnez a6, group_bad
 bnez a7, group_bad
 li s9, 6
 li a0, 0
 li a1, 0
 li a2, 0
 li a3, 0
 li a4, 0
 li a5, 0
 li a6, 0
 li a1, 1
 li a7, SYS_GETPG
 ecall
 li t1, -1
 bne a0, t1, group_bad
 bnez t0, group_bad
 bnez a1, group_bad
 bnez a2, group_bad
 bnez a3, group_bad
 bnez a4, group_bad
 bnez a5, group_bad
 bnez a6, group_bad
 bnez a7, group_bad
 li s9, 7
 li a0, 0
 li a1, 0
 li a2, 0
 li a3, 0
 li a4, 0
 li a5, 0
 li a6, 0
 li a0, -1
 li a7, SYS_SETPG
 ecall
 li t1, -1
 bne a0, t1, group_bad
 bnez t0, group_bad
 bnez a1, group_bad
 bnez a2, group_bad
 bnez a3, group_bad
 bnez a4, group_bad
 bnez a5, group_bad
 bnez a6, group_bad
 bnez a7, group_bad
 li s9, 8
 li a0, 0
 li a1, 0
 li a2, 0
 li a3, 0
 li a4, 0
 li a5, 0
 li a6, 0
 li a1, -1
 li a7, SYS_SETPG
 ecall
 li t1, -1
 bne a0, t1, group_bad
 bnez t0, group_bad
 bnez a1, group_bad
 bnez a2, group_bad
 bnez a3, group_bad
 bnez a4, group_bad
 bnez a5, group_bad
 bnez a6, group_bad
 bnez a7, group_bad
 li s9, 9
 li a0, 0
 li a1, 0
 li a2, 0
 li a3, 0
 li a4, 0
 li a5, 0
 li a6, 0
 li a1, 576460752303423488
 li a7, SYS_SETPG
 ecall
 li t1, -3
 bne a0, t1, group_bad
 bnez t0, group_bad
 bnez a1, group_bad
 bnez a2, group_bad
 bnez a3, group_bad
 bnez a4, group_bad
 bnez a5, group_bad
 bnez a6, group_bad
 bnez a7, group_bad
 li s9, 10
 li a0, 0
 li a1, 0
 li a2, 0
 li a3, 0
 li a4, 0
 li a5, 0
 li a6, 0
 li a2, 1
 li a7, SYS_SETPG
 ecall
 li t1, -1
 bne a0, t1, group_bad
 bnez t0, group_bad
 bnez a1, group_bad
 bnez a2, group_bad
 bnez a3, group_bad
 bnez a4, group_bad
 bnez a5, group_bad
 bnez a6, group_bad
 bnez a7, group_bad
 li s9, 11
 li a0, 0
 li a1, 0
 li a2, 0
 li a3, 0
 li a4, 0
 li a5, 0
 li a6, 0
 li a0, 1
 li a7, SYS_SETSID
 ecall
 li t1, -1
 bne a0, t1, group_bad
 bnez t0, group_bad
 bnez a1, group_bad
 bnez a2, group_bad
 bnez a3, group_bad
 bnez a4, group_bad
 bnez a5, group_bad
 bnez a6, group_bad
 bnez a7, group_bad
 li s9, 12
 li a0, 0
 li a1, 0
 li a2, 0
 li a3, 0
 li a4, 0
 li a5, 0
 li a6, 0
 mv a0, s1
 li a7, SYS_SETPG
 ecall
 li t1, -5
 bne a0, t1, group_bad
 bnez t0, group_bad
 bnez a1, group_bad
 bnez a2, group_bad
 bnez a3, group_bad
 bnez a4, group_bad
 bnez a5, group_bad
 bnez a6, group_bad
 bnez a7, group_bad
 li s9, 13
 li a0, 0
 li a1, 0
 li a2, 0
 li a3, 0
 li a4, 0
 li a5, 0
 li a6, 0
 mv a0, s0
 li a7, SYS_SETPG
 ecall
 bnez a0, group_bad
 bnez t0, group_bad
 bnez a1, group_bad
 bnez a2, group_bad
 bnez a3, group_bad
 bnez a4, group_bad
 bnez a5, group_bad
 bnez a6, group_bad
 bnez a7, group_bad
 li s9, 14
 li a0, 0
 li a1, 0
 li a2, 0
 li a3, 0
 li a4, 0
 li a5, 0
 li a6, 0
 mv a0, s0
 li a7, SYS_GETPG
 ecall
 bnez a0, group_bad
 bnez t0, group_bad
 mv t1, s0
 bne a1, t1, group_bad
 li t1, 1
 bne a2, t1, group_bad
 bnez a3, group_bad
 bnez a4, group_bad
 bnez a5, group_bad
 bnez a6, group_bad
 bnez a7, group_bad
 li s9, 15
 li a0, 0
 li a1, 0
 li a2, 0
 li a3, 0
 li a4, 0
 li a5, 0
 li a6, 0
 mv a1, s0
 li a7, SYS_SETPG
 ecall
 bnez a0, group_bad
 bnez t0, group_bad
 bnez a1, group_bad
 bnez a2, group_bad
 bnez a3, group_bad
 bnez a4, group_bad
 bnez a5, group_bad
 bnez a6, group_bad
 bnez a7, group_bad
 li s9, 16
 li a0, 0
 li a1, 0
 li a2, 0
 li a3, 0
 li a4, 0
 li a5, 0
 li a6, 0
 li a7, SYS_SETPG
 ecall
 bnez a0, group_bad
 bnez t0, group_bad
 bnez a1, group_bad
 bnez a2, group_bad
 bnez a3, group_bad
 bnez a4, group_bad
 bnez a5, group_bad
 bnez a6, group_bad
 bnez a7, group_bad
 li s9, 17
 li a0, 0
 li a1, 0
 li a2, 0
 li a3, 0
 li a4, 0
 li a5, 0
 li a6, 0
 mv a0, s0
 mv a1, s2
 li a7, SYS_SETPG
 ecall
 bnez a0, group_bad
 bnez t0, group_bad
 bnez a1, group_bad
 bnez a2, group_bad
 bnez a3, group_bad
 bnez a4, group_bad
 bnez a5, group_bad
 bnez a6, group_bad
 bnez a7, group_bad
 li s9, 18
 li a0, 0
 li a1, 0
 li a2, 0
 li a3, 0
 li a4, 0
 li a5, 0
 li a6, 0
 li a1, 1
 li a7, SYS_SETPG
 ecall
 bnez a0, group_bad
 bnez t0, group_bad
 bnez a1, group_bad
 bnez a2, group_bad
 bnez a3, group_bad
 bnez a4, group_bad
 bnez a5, group_bad
 bnez a6, group_bad
 bnez a7, group_bad
 li s9, 19
 li a0, 0
 li a1, 0
 li a2, 0
 li a3, 0
 li a4, 0
 li a5, 0
 li a6, 0
 li a7, SYS_SETSID
 ecall
 li t1, -5
 bne a0, t1, group_bad
 bnez t0, group_bad
 bnez a1, group_bad
 bnez a2, group_bad
 bnez a3, group_bad
 bnez a4, group_bad
 bnez a5, group_bad
 bnez a6, group_bad
 bnez a7, group_bad
 li s9, 20
 li a0, 0
 li a1, 0
 li a2, 0
 li a3, 0
 li a4, 0
 li a5, 0
 li a6, 0
 mv a0, s0
 li a7, SYS_SETPG
 ecall
 bnez a0, group_bad
 bnez t0, group_bad
 bnez a1, group_bad
 bnez a2, group_bad
 bnez a3, group_bad
 bnez a4, group_bad
 bnez a5, group_bad
 bnez a6, group_bad
 bnez a7, group_bad
 li s9, 21
 li a0, 0
 li a1, 0
 li a2, 0
 li a3, 0
 li a4, 0
 li a5, 0
 li a6, 0
 li a7, SYS_SETSID
 ecall
 bnez a0, group_bad
 bnez t0, group_bad
 mv t1, s2
 bne a1, t1, group_bad
 bnez a2, group_bad
 bnez a3, group_bad
 bnez a4, group_bad
 bnez a5, group_bad
 bnez a6, group_bad
 bnez a7, group_bad
 li s9, 22
 li a0, 0
 li a1, 0
 li a2, 0
 li a3, 0
 li a4, 0
 li a5, 0
 li a6, 0
 li a7, SYS_GETPG
 ecall
 bnez a0, group_bad
 bnez t0, group_bad
 mv t1, s2
 bne a1, t1, group_bad
 mv t1, s2
 bne a2, t1, group_bad
 bnez a3, group_bad
 bnez a4, group_bad
 bnez a5, group_bad
 bnez a6, group_bad
 bnez a7, group_bad
 li s9, 23
 li a0, 0
 li a1, 0
 li a2, 0
 li a3, 0
 li a4, 0
 li a5, 0
 li a6, 0
 mv a0, s0
 li a7, SYS_GETPG
 ecall
 li t1, -5
 bne a0, t1, group_bad
 bnez t0, group_bad
 bnez a1, group_bad
 bnez a2, group_bad
 bnez a3, group_bad
 bnez a4, group_bad
 bnez a5, group_bad
 bnez a6, group_bad
 bnez a7, group_bad
 li s9, 24
 li a0, 0
 li a1, 0
 li a2, 0
 li a3, 0
 li a4, 0
 li a5, 0
 li a6, 0
 mv a0, s0
 li a7, SYS_SETPG
 ecall
 li t1, -5
 bne a0, t1, group_bad
 bnez t0, group_bad
 bnez a1, group_bad
 bnez a2, group_bad
 bnez a3, group_bad
 bnez a4, group_bad
 bnez a5, group_bad
 bnez a6, group_bad
 bnez a7, group_bad
 li s9, 25
 li a0, 0
 li a1, 0
 li a2, 0
 li a3, 0
 li a4, 0
 li a5, 0
 li a6, 0
 li a7, SYS_SETPG
 ecall
 li t1, -5
 bne a0, t1, group_bad
 bnez t0, group_bad
 bnez a1, group_bad
 bnez a2, group_bad
 bnez a3, group_bad
 bnez a4, group_bad
 bnez a5, group_bad
 bnez a6, group_bad
 bnez a7, group_bad
 li s9, 26
 li a0, 0
 li a1, 0
 li a2, 0
 li a3, 0
 li a4, 0
 li a5, 0
 li a6, 0
 li a7, SYS_SETSID
 ecall
 li t1, -5
 bne a0, t1, group_bad
 bnez t0, group_bad
 bnez a1, group_bad
 bnez a2, group_bad
 bnez a3, group_bad
 bnez a4, group_bad
 bnez a5, group_bad
 bnez a6, group_bad
 bnez a7, group_bad
 li t0, 262272
 li t1, 1
 sd t1, 0(t0)
 li s9, 27
 li a0, 0
 li a1, 0
 li a2, 0
 li a3, 0
 li a4, 0
 li a5, 0
 li a6, 0
 li a7, SYS_YIELD
 ecall
 bnez a0, group_bad
 bnez t0, group_bad
 bnez a1, group_bad
 bnez a2, group_bad
 bnez a3, group_bad
 bnez a4, group_bad
 bnez a5, group_bad
 bnez a6, group_bad
 bnez a7, group_bad
 li t0, 262144
 ld s3, 24(t0)
 li s9, 28
 li a0, 0
 li a1, 0
 li a2, 0
 li a3, 0
 li a4, 0
 li a5, 0
 li a6, 0
 mv a0, s3
 li a7, SYS_GETPG
 ecall
 bnez a0, group_bad
 bnez t0, group_bad
 mv t1, s2
 bne a1, t1, group_bad
 mv t1, s2
 bne a2, t1, group_bad
 bnez a3, group_bad
 bnez a4, group_bad
 bnez a5, group_bad
 bnez a6, group_bad
 bnez a7, group_bad
 li s8, 42
 j group_exit
group_descendant:
 li t0, 262144
 ld s0, 0(t0)
 li t0, 262144
 ld s1, 8(t0)
 li t0, 262144
 ld s2, 16(t0)
 li t0, 262144
 ld s3, 24(t0)
 li t0, 262144
 ld s4, 32(t0)
 li t0, 262144
 ld s5, 40(t0)
 li t0, 262144
 ld s6, 48(t0)
 li t0, 4660
 fmv.d.x f31, t0
 li s9, 29
 li a0, 0
 li a1, 0
 li a2, 0
 li a3, 0
 li a4, 0
 li a5, 0
 li a6, 0
 li a7, SYS_GETPG
 ecall
 bnez a0, group_bad
 bnez t0, group_bad
 mv t1, s4
 bne a1, t1, group_bad
 mv t1, s4
 bne a2, t1, group_bad
 bnez a3, group_bad
 bnez a4, group_bad
 bnez a5, group_bad
 bnez a6, group_bad
 bnez a7, group_bad
 li s9, 30
 li a0, 0
 li a1, 0
 li a2, 0
 li a3, 0
 li a4, 0
 li a5, 0
 li a6, 0
 mv a0, s4
 li a7, SYS_GETPG
 ecall
 li t1, -4
 bne a0, t1, group_bad
 bnez t0, group_bad
 bnez a1, group_bad
 bnez a2, group_bad
 bnez a3, group_bad
 bnez a4, group_bad
 bnez a5, group_bad
 bnez a6, group_bad
 bnez a7, group_bad
 li s9, 31
 li a0, 0
 li a1, 0
 li a2, 0
 li a3, 0
 li a4, 0
 li a5, 0
 li a6, 0
 mv a0, s5
 li a7, SYS_GETPG
 ecall
 li t1, -5
 bne a0, t1, group_bad
 bnez t0, group_bad
 bnez a1, group_bad
 bnez a2, group_bad
 bnez a3, group_bad
 bnez a4, group_bad
 bnez a5, group_bad
 bnez a6, group_bad
 bnez a7, group_bad
 li s9, 32
 li a0, 0
 li a1, 0
 li a2, 0
 li a3, 0
 li a4, 0
 li a5, 0
 li a6, 0
 li a7, SYS_SETPG
 ecall
 bnez a0, group_bad
 bnez t0, group_bad
 bnez a1, group_bad
 bnez a2, group_bad
 bnez a3, group_bad
 bnez a4, group_bad
 bnez a5, group_bad
 bnez a6, group_bad
 bnez a7, group_bad
 li s9, 33
 li a0, 0
 li a1, 0
 li a2, 0
 li a3, 0
 li a4, 0
 li a5, 0
 li a6, 0
 li a7, SYS_GETPG
 ecall
 bnez a0, group_bad
 bnez t0, group_bad
 mv t1, s2
 bne a1, t1, group_bad
 mv t1, s4
 bne a2, t1, group_bad
 bnez a3, group_bad
 bnez a4, group_bad
 bnez a5, group_bad
 bnez a6, group_bad
 bnez a7, group_bad
 li s9, 34
 li a0, 0
 li a1, 0
 li a2, 0
 li a3, 0
 li a4, 0
 li a5, 0
 li a6, 0
 li a7, SYS_SETSID
 ecall
 li t1, -5
 bne a0, t1, group_bad
 bnez t0, group_bad
 bnez a1, group_bad
 bnez a2, group_bad
 bnez a3, group_bad
 bnez a4, group_bad
 bnez a5, group_bad
 bnez a6, group_bad
 bnez a7, group_bad
 li s9, 35
 li a0, 0
 li a1, 0
 li a2, 0
 li a3, 0
 li a4, 0
 li a5, 0
 li a6, 0
 mv a1, s4
 li a7, SYS_SETPG
 ecall
 bnez a0, group_bad
 bnez t0, group_bad
 bnez a1, group_bad
 bnez a2, group_bad
 bnez a3, group_bad
 bnez a4, group_bad
 bnez a5, group_bad
 bnez a6, group_bad
 bnez a7, group_bad
 li s9, 36
 li a0, 0
 li a1, 0
 li a2, 0
 li a3, 0
 li a4, 0
 li a5, 0
 li a6, 0
 li a7, SYS_SETSID
 ecall
 bnez a0, group_bad
 bnez t0, group_bad
 mv t1, s2
 bne a1, t1, group_bad
 bnez a2, group_bad
 bnez a3, group_bad
 bnez a4, group_bad
 bnez a5, group_bad
 bnez a6, group_bad
 bnez a7, group_bad
 li s9, 37
 li a0, 0
 li a1, 0
 li a2, 0
 li a3, 0
 li a4, 0
 li a5, 0
 li a6, 0
 li a7, SYS_GETPG
 ecall
 bnez a0, group_bad
 bnez t0, group_bad
 mv t1, s2
 bne a1, t1, group_bad
 mv t1, s2
 bne a2, t1, group_bad
 bnez a3, group_bad
 bnez a4, group_bad
 bnez a5, group_bad
 bnez a6, group_bad
 bnez a7, group_bad
 li s8, 42
 j group_exit
group_bad:
 addi s8, s9, 100
group_exit:
 li a0, 0
 li a1, 0
 li a2, 0
 li a3, 0
 li a4, 0
 li a5, 0
 li a6, 0
 mv a0, s8
 li a7, SYS_EXIT
 ecall
 j group_bad
group_end:
