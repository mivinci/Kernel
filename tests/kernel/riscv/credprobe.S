#include <abi/syscall.h>
#include <abi/ipc.h>
.option norvc
.section .rodata
.balign 8
.global cred_begin, cred_end, cred_server, cred_replacement
cred_begin:
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
 li a7, SYS_GETCRED
 ecall
 beqz a0, 99f
 j cred_bad
99:
 beqz t0, 99f
 j cred_bad
99:
 li t1, 10
 beq a1, t1, 99f
 j cred_bad
99:
 li t1, 20
 beq a2, t1, 99f
 j cred_bad
99:
 li t1, 30
 beq a3, t1, 99f
 j cred_bad
99:
 li t1, 40
 beq a4, t1, 99f
 j cred_bad
99:
 beqz a5, 99f
 j cred_bad
99:
 beqz a6, 99f
 j cred_bad
99:
 beqz a7, 99f
 j cred_bad
99:
 li t0, 262144
 ld s8, 80(t0)
 li s9, 2
 li a0, 0
 li a1, 0
 li a2, 0
 li a3, 0
 li a4, 0
 li a5, 0
 li a6, 0
 mv a0, s8
 li a1, 5
 li a3, 263168
 li a4, 2
 li a7, SYS_QUERY
 ecall
 beqz a0, 99f
 j cred_bad
99:
 beqz t0, 99f
 j cred_bad
99:
 li t1, 2
 beq a1, t1, 99f
 j cred_bad
99:
 beqz a2, 99f
 j cred_bad
99:
 beqz a3, 99f
 j cred_bad
99:
 beqz a4, 99f
 j cred_bad
99:
 beqz a5, 99f
 j cred_bad
99:
 beqz a6, 99f
 j cred_bad
99:
 beqz a7, 99f
 j cred_bad
99:
 li t0, 263168
 ld t2, 0(t0)
 li t1, 5
 beq t2, t1, 99f
 j cred_bad
99:
 li t0, 263168
 ld t2, 8(t0)
 mv t1, s3
 beq t2, t1, 99f
 j cred_bad
99:
 li t0, 263168
 ld t2, 16(t0)
 li t1, 1
 beq t2, t1, 99f
 j cred_bad
99:
 li t0, 263168
 ld t2, 40(t0)
 li t1, 2
 beq t2, t1, 99f
 j cred_bad
99:
 li t0, 262144
 ld s7, 88(t0)
 li t0, 263168
 ld t2, 32(t0)
 mv t1, s7
 beq t2, t1, 99f
 j cred_bad
99:
 li t0, 263360
 ld t2, 0(t0)
 li t1, 5
 beq t2, t1, 99f
 j cred_bad
99:
 li t0, 263360
 ld t2, 8(t0)
 mv t1, s3
 beq t2, t1, 99f
 j cred_bad
99:
 li t0, 263360
 ld t2, 16(t0)
 li t1, 1
 beq t2, t1, 99f
 j cred_bad
99:
 li t0, 263360
 ld t2, 40(t0)
 li t1, 3
 beq t2, t1, 99f
 j cred_bad
99:
 li t0, 262144
 ld s7, 96(t0)
 li t0, 263360
 ld t2, 32(t0)
 mv t1, s7
 beq t2, t1, 99f
 j cred_bad
99:
 li s9, 3
 li a0, 0
 li a1, 0
 li a2, 0
 li a3, 0
 li a4, 0
 li a5, 0
 li a6, 0
 mv a0, s8
 li a1, 1
 li a3, 263168
 li a4, 2
 li a7, SYS_QUERY
 ecall
 li t1, -5
 beq a0, t1, 99f
 j cred_bad
99:
 beqz t0, 99f
 j cred_bad
99:
 beqz a1, 99f
 j cred_bad
99:
 beqz a2, 99f
 j cred_bad
99:
 beqz a3, 99f
 j cred_bad
99:
 beqz a4, 99f
 j cred_bad
99:
 beqz a5, 99f
 j cred_bad
99:
 beqz a6, 99f
 j cred_bad
99:
 beqz a7, 99f
 j cred_bad
99:
 li s9, 4
 li a0, 0
 li a1, 0
 li a2, 0
 li a3, 0
 li a4, 0
 li a5, 0
 li a6, 0
 mv a0, s8
 li a1, 2
 li a3, 263168
 li a4, 2
 li a7, SYS_QUERY
 ecall
 li t1, -5
 beq a0, t1, 99f
 j cred_bad
99:
 beqz t0, 99f
 j cred_bad
99:
 beqz a1, 99f
 j cred_bad
99:
 beqz a2, 99f
 j cred_bad
99:
 beqz a3, 99f
 j cred_bad
99:
 beqz a4, 99f
 j cred_bad
99:
 beqz a5, 99f
 j cred_bad
99:
 beqz a6, 99f
 j cred_bad
99:
 beqz a7, 99f
 j cred_bad
99:
 li s9, 5
 li a0, 0
 li a1, 0
 li a2, 0
 li a3, 0
 li a4, 0
 li a5, 0
 li a6, 0
 mv a0, s8
 li a1, 3
 li a3, 263168
 li a4, 2
 li a7, SYS_QUERY
 ecall
 li t1, -5
 beq a0, t1, 99f
 j cred_bad
99:
 beqz t0, 99f
 j cred_bad
99:
 beqz a1, 99f
 j cred_bad
99:
 beqz a2, 99f
 j cred_bad
99:
 beqz a3, 99f
 j cred_bad
99:
 beqz a4, 99f
 j cred_bad
99:
 beqz a5, 99f
 j cred_bad
99:
 beqz a6, 99f
 j cred_bad
99:
 beqz a7, 99f
 j cred_bad
99:
 li s9, 6
 li a0, 0
 li a1, 0
 li a2, 0
 li a3, 0
 li a4, 0
 li a5, 0
 li a6, 0
 mv a0, s8
 li a1, 4
 li a3, 263168
 li a4, 2
 li a7, SYS_QUERY
 ecall
 li t1, -5
 beq a0, t1, 99f
 j cred_bad
99:
 beqz t0, 99f
 j cred_bad
99:
 beqz a1, 99f
 j cred_bad
99:
 beqz a2, 99f
 j cred_bad
99:
 beqz a3, 99f
 j cred_bad
99:
 beqz a4, 99f
 j cred_bad
99:
 beqz a5, 99f
 j cred_bad
99:
 beqz a6, 99f
 j cred_bad
99:
 beqz a7, 99f
 j cred_bad
99:
 li s9, 7
 li a0, 0
 li a1, 0
 li a2, 0
 li a3, 0
 li a4, 0
 li a5, 0
 li a6, 0
 mv a0, s8
 li a1, 6
 li a3, 263168
 li a4, 2
 li a7, SYS_QUERY
 ecall
 li t1, -5
 beq a0, t1, 99f
 j cred_bad
99:
 beqz t0, 99f
 j cred_bad
99:
 beqz a1, 99f
 j cred_bad
99:
 beqz a2, 99f
 j cred_bad
99:
 beqz a3, 99f
 j cred_bad
99:
 beqz a4, 99f
 j cred_bad
99:
 beqz a5, 99f
 j cred_bad
99:
 beqz a6, 99f
 j cred_bad
99:
 beqz a7, 99f
 j cred_bad
99:
 li s9, 8
 li a0, 0
 li a1, 0
 li a2, 0
 li a3, 0
 li a4, 0
 li a5, 0
 li a6, 0
 mv a0, s8
 li a1, 5
 li a3, 274861129728
 li a4, 2
 li a7, SYS_QUERY
 ecall
 li t1, -2
 beq a0, t1, 99f
 j cred_bad
99:
 beqz t0, 99f
 j cred_bad
99:
 beqz a1, 99f
 j cred_bad
99:
 beqz a2, 99f
 j cred_bad
99:
 beqz a3, 99f
 j cred_bad
99:
 beqz a4, 99f
 j cred_bad
99:
 beqz a5, 99f
 j cred_bad
99:
 beqz a6, 99f
 j cred_bad
99:
 beqz a7, 99f
 j cred_bad
99:
 li s9, 9
 li a0, 0
 li a1, 0
 li a2, 0
 li a3, 0
 li a4, 0
 li a5, 0
 li a6, 0
 mv a0, s8
 li a1, 262144
 li a7, SYS_ALLOC
 ecall
 li t1, -5
 beq a0, t1, 99f
 j cred_bad
99:
 beqz t0, 99f
 j cred_bad
99:
 beqz a1, 99f
 j cred_bad
99:
 beqz a2, 99f
 j cred_bad
99:
 beqz a3, 99f
 j cred_bad
99:
 beqz a4, 99f
 j cred_bad
99:
 beqz a5, 99f
 j cred_bad
99:
 beqz a6, 99f
 j cred_bad
99:
 beqz a7, 99f
 j cred_bad
99:
 li s9, 10
 li a0, 0
 li a1, 0
 li a2, 0
 li a3, 0
 li a4, 0
 li a5, 0
 li a6, 0
 mv a1, s5
 li a7, SYS_GETCRED
 ecall
 li t1, -1
 beq a0, t1, 99f
 j cred_bad
99:
 beqz t0, 99f
 j cred_bad
99:
 beqz a1, 99f
 j cred_bad
99:
 beqz a2, 99f
 j cred_bad
99:
 beqz a3, 99f
 j cred_bad
99:
 beqz a4, 99f
 j cred_bad
99:
 beqz a5, 99f
 j cred_bad
99:
 beqz a6, 99f
 j cred_bad
99:
 beqz a7, 99f
 j cred_bad
99:
 li s9, 11
 li a0, 0
 li a1, 0
 li a2, 0
 li a3, 0
 li a4, 0
 li a5, 0
 li a6, 0
 mv a0, s4
 li a7, SYS_GETCRED
 ecall
 li t1, -3
 beq a0, t1, 99f
 j cred_bad
99:
 beqz t0, 99f
 j cred_bad
99:
 beqz a1, 99f
 j cred_bad
99:
 beqz a2, 99f
 j cred_bad
99:
 beqz a3, 99f
 j cred_bad
99:
 beqz a4, 99f
 j cred_bad
99:
 beqz a5, 99f
 j cred_bad
99:
 beqz a6, 99f
 j cred_bad
99:
 beqz a7, 99f
 j cred_bad
99:
 li s9, 12
 li a0, 0
 li a1, 0
 li a2, 0
 li a3, 0
 li a4, 0
 li a5, 0
 li a6, 0
 mv a0, s0
 li a7, SYS_GETCRED
 ecall
 li t1, -3
 beq a0, t1, 99f
 j cred_bad
99:
 beqz t0, 99f
 j cred_bad
99:
 beqz a1, 99f
 j cred_bad
99:
 beqz a2, 99f
 j cred_bad
99:
 beqz a3, 99f
 j cred_bad
99:
 beqz a4, 99f
 j cred_bad
99:
 beqz a5, 99f
 j cred_bad
99:
 beqz a6, 99f
 j cred_bad
99:
 beqz a7, 99f
 j cred_bad
99:
 li s9, 13
 li a0, 0
 li a1, 0
 li a2, 0
 li a3, 0
 li a4, 0
 li a5, 0
 li a6, 0
 mv a0, s2
 li a7, SYS_GETCRED
 ecall
 li t1, -5
 beq a0, t1, 99f
 j cred_bad
99:
 beqz t0, 99f
 j cred_bad
99:
 beqz a1, 99f
 j cred_bad
99:
 beqz a2, 99f
 j cred_bad
99:
 beqz a3, 99f
 j cred_bad
99:
 beqz a4, 99f
 j cred_bad
99:
 beqz a5, 99f
 j cred_bad
99:
 beqz a6, 99f
 j cred_bad
99:
 beqz a7, 99f
 j cred_bad
99:
 li s9, 14
 li a0, 0
 li a1, 0
 li a2, 0
 li a3, 0
 li a4, 0
 li a5, 0
 li a6, 0
 mv a0, s1
 mv a1, s5
 li a7, SYS_GETCRED
 ecall
 li t1, -1
 beq a0, t1, 99f
 j cred_bad
99:
 beqz t0, 99f
 j cred_bad
99:
 beqz a1, 99f
 j cred_bad
99:
 beqz a2, 99f
 j cred_bad
99:
 beqz a3, 99f
 j cred_bad
99:
 beqz a4, 99f
 j cred_bad
99:
 beqz a5, 99f
 j cred_bad
99:
 beqz a6, 99f
 j cred_bad
99:
 beqz a7, 99f
 j cred_bad
99:
 li s9, 15
 li a0, 0
 li a1, 0
 li a2, 0
 li a3, 0
 li a4, 0
 li a5, 0
 li a6, 0
 mv a0, s1
 li a3, 1
 li a7, SYS_GETCRED
 ecall
 li t1, -1
 beq a0, t1, 99f
 j cred_bad
99:
 beqz t0, 99f
 j cred_bad
99:
 beqz a1, 99f
 j cred_bad
99:
 beqz a2, 99f
 j cred_bad
99:
 beqz a3, 99f
 j cred_bad
99:
 beqz a4, 99f
 j cred_bad
99:
 beqz a5, 99f
 j cred_bad
99:
 beqz a6, 99f
 j cred_bad
99:
 beqz a7, 99f
 j cred_bad
99:
 li s9, 16
 li a0, 0
 li a1, 0
 li a2, 0
 li a3, 0
 li a4, 0
 li a5, 0
 li a6, 0
 mv a0, s1
 li a7, SYS_GETCRED
 ecall
 beqz a0, 99f
 j cred_bad
99:
 beqz t0, 99f
 j cred_bad
99:
 li t1, 50
 beq a1, t1, 99f
 j cred_bad
99:
 li t1, 60
 beq a2, t1, 99f
 j cred_bad
99:
 li t1, 70
 beq a3, t1, 99f
 j cred_bad
99:
 li t1, 80
 beq a4, t1, 99f
 j cred_bad
99:
 beqz a5, 99f
 j cred_bad
99:
 beqz a6, 99f
 j cred_bad
99:
 beqz a7, 99f
 j cred_bad
99:
 li s9, 17
 li a0, 0
 li a1, 0
 li a2, 0
 li a3, 0
 li a4, 0
 li a5, 0
 li a6, 0
 mv a0, s1
 mv a1, s4
 li a2, 1
 li a7, SYS_GETCRED
 ecall
 beqz a0, 99f
 j cred_bad
99:
 beqz t0, 99f
 j cred_bad
99:
 li t1, 50
 beq a1, t1, 99f
 j cred_bad
99:
 li t1, 60
 beq a2, t1, 99f
 j cred_bad
99:
 li t1, 70
 beq a3, t1, 99f
 j cred_bad
99:
 li t1, 80
 beq a4, t1, 99f
 j cred_bad
99:
 beqz a5, 99f
 j cred_bad
99:
 beqz a6, 99f
 j cred_bad
99:
 beqz a7, 99f
 j cred_bad
99:
 li t0, 262144
 ld t2, 104(t0)
 li s9, 18
 li a0, 0
 li a1, 0
 li a2, 0
 li a3, 0
 li a4, 0
 li a5, 0
 li a6, 0
 mv a0, t2
 mv a1, s1
 li a2, 3
 li a7, SYS_OBSERVE
 ecall
 beqz a0, 99f
 j cred_bad
99:
 beqz t0, 99f
 j cred_bad
99:
 li t1, 2
 beq a2, t1, 99f
 j cred_bad
99:
 beqz a3, 99f
 j cred_bad
99:
 beqz a4, 99f
 j cred_bad
99:
 beqz a5, 99f
 j cred_bad
99:
 beqz a6, 99f
 j cred_bad
99:
 beqz a7, 99f
 j cred_bad
99:
 mv s8, a1
 li s9, 19
 li a0, 0
 li a1, 0
 li a2, 0
 li a3, 0
 li a4, 0
 li a5, 0
 li a6, 0
 mv a0, s8
 li a7, SYS_OBSREAD
 ecall
 beqz a0, 99f
 j cred_bad
99:
 beqz t0, 99f
 j cred_bad
99:
 li t1, 2
 beq a1, t1, 99f
 j cred_bad
99:
 beqz a2, 99f
 j cred_bad
99:
 mv t1, s4
 beq a3, t1, 99f
 j cred_bad
99:
 li t1, 1
 beq a4, t1, 99f
 j cred_bad
99:
 beqz a5, 99f
 j cred_bad
99:
 beqz a6, 99f
 j cred_bad
99:
 beqz a7, 99f
 j cred_bad
99:
 li s9, 20
 li a0, 0
 li a1, 0
 li a2, 0
 li a3, 0
 li a4, 0
 li a5, 0
 li a6, 0
 mv a0, s8
 li a7, SYS_OBSACK
 ecall
 li t1, -6
 beq a0, t1, 99f
 j cred_bad
99:
 beqz t0, 99f
 j cred_bad
99:
 beqz a1, 99f
 j cred_bad
99:
 beqz a2, 99f
 j cred_bad
99:
 beqz a3, 99f
 j cred_bad
99:
 beqz a4, 99f
 j cred_bad
99:
 beqz a5, 99f
 j cred_bad
99:
 beqz a6, 99f
 j cred_bad
99:
 beqz a7, 99f
 j cred_bad
99:
 li t0, 262144
 ld t2, 104(t0)
 li s9, 21
 li a0, 0
 li a1, 0
 li a2, 0
 li a3, 0
 li a4, 0
 li a5, 0
 li a6, 0
 mv a0, t2
 mv a1, s1
 li a2, 3
 mv a3, s5
 li a7, SYS_OBSERVE
 ecall
 li t1, -1
 beq a0, t1, 99f
 j cred_bad
99:
 beqz t0, 99f
 j cred_bad
99:
 beqz a1, 99f
 j cred_bad
99:
 beqz a2, 99f
 j cred_bad
99:
 beqz a3, 99f
 j cred_bad
99:
 beqz a4, 99f
 j cred_bad
99:
 beqz a5, 99f
 j cred_bad
99:
 beqz a6, 99f
 j cred_bad
99:
 beqz a7, 99f
 j cred_bad
99:
 li t0, 262144
 ld t2, 104(t0)
 li s9, 22
 li a0, 0
 li a1, 0
 li a2, 0
 li a3, 0
 li a4, 0
 li a5, 0
 li a6, 0
 mv a0, t2
 li a2, 3
 mv a3, s4
 li a7, SYS_OBSERVE
 ecall
 li t1, -3
 beq a0, t1, 99f
 j cred_bad
99:
 beqz t0, 99f
 j cred_bad
99:
 beqz a1, 99f
 j cred_bad
99:
 beqz a2, 99f
 j cred_bad
99:
 beqz a3, 99f
 j cred_bad
99:
 beqz a4, 99f
 j cred_bad
99:
 beqz a5, 99f
 j cred_bad
99:
 beqz a6, 99f
 j cred_bad
99:
 beqz a7, 99f
 j cred_bad
99:
 li s9, 23
 li a0, 0
 li a1, 0
 li a2, 0
 li a3, 0
 li a4, 0
 li a5, 0
 li a6, 0
 mv a0, s1
 mv a1, s1
 li a2, 3
 li a7, SYS_OBSERVE
 ecall
 li t1, -3
 beq a0, t1, 99f
 j cred_bad
99:
 beqz t0, 99f
 j cred_bad
99:
 beqz a1, 99f
 j cred_bad
99:
 beqz a2, 99f
 j cred_bad
99:
 beqz a3, 99f
 j cred_bad
99:
 beqz a4, 99f
 j cred_bad
99:
 beqz a5, 99f
 j cred_bad
99:
 beqz a6, 99f
 j cred_bad
99:
 beqz a7, 99f
 j cred_bad
99:
 li s9, 24
 li a0, 0
 li a1, 0
 li a2, 0
 li a3, 0
 li a4, 0
 li a5, 0
 li a6, 0
 mv a0, s8
 li a7, SYS_OBSDROP
 ecall
 beqz a0, 99f
 j cred_bad
99:
 beqz t0, 99f
 j cred_bad
99:
 beqz a1, 99f
 j cred_bad
99:
 beqz a2, 99f
 j cred_bad
99:
 beqz a3, 99f
 j cred_bad
99:
 beqz a4, 99f
 j cred_bad
99:
 beqz a5, 99f
 j cred_bad
99:
 beqz a6, 99f
 j cred_bad
99:
 beqz a7, 99f
 j cred_bad
99:
 li s9, 25
 li a0, 0
 li a1, 0
 li a2, 0
 li a3, 0
 li a4, 0
 li a5, 0
 li a6, 0
 mv a0, s8
 li a7, SYS_OBSREAD
 ecall
 li t1, -4
 beq a0, t1, 99f
 j cred_bad
99:
 beqz t0, 99f
 j cred_bad
99:
 beqz a1, 99f
 j cred_bad
99:
 beqz a2, 99f
 j cred_bad
99:
 beqz a3, 99f
 j cred_bad
99:
 beqz a4, 99f
 j cred_bad
99:
 beqz a5, 99f
 j cred_bad
99:
 beqz a6, 99f
 j cred_bad
99:
 beqz a7, 99f
 j cred_bad
99:
 li t0, 262144
 ld t2, 104(t0)
 li s9, 26
 li a0, 0
 li a1, 0
 li a2, 0
 li a3, 0
 li a4, 0
 li a5, 0
 li a6, 0
 mv a0, t2
 mv a1, s1
 li a2, 3
 li a7, SYS_OBSERVE
 ecall
 beqz a0, 99f
 j cred_bad
99:
 beqz t0, 99f
 j cred_bad
99:
 li t1, 2
 beq a2, t1, 99f
 j cred_bad
99:
 beqz a3, 99f
 j cred_bad
99:
 beqz a4, 99f
 j cred_bad
99:
 beqz a5, 99f
 j cred_bad
99:
 beqz a6, 99f
 j cred_bad
99:
 beqz a7, 99f
 j cred_bad
99:
 li t0, 262144
 ld s8, 80(t0)
 li t0, 262144
 ld t2, 104(t0)
 li s9, 27
 li a0, 0
 li a1, 0
 li a2, 0
 li a3, 0
 li a4, 0
 li a5, 0
 li a6, 0
 mv a0, s8
 mv a1, t2
 li a2, 1
 li a5, 1
 li a7, SYS_GRANT
 ecall
 li t1, -1
 beq a0, t1, 99f
 j cred_bad
99:
 beqz t0, 99f
 j cred_bad
99:
 beqz a1, 99f
 j cred_bad
99:
 beqz a2, 99f
 j cred_bad
99:
 beqz a3, 99f
 j cred_bad
99:
 beqz a4, 99f
 j cred_bad
99:
 beqz a5, 99f
 j cred_bad
99:
 beqz a6, 99f
 j cred_bad
99:
 beqz a7, 99f
 j cred_bad
99:
 li s9, 28
 li a0, 0
 li a1, 0
 li a2, 0
 li a3, 0
 li a4, 0
 li a5, 0
 li a6, 0
 mv a0, s8
 mv a1, t2
 li a2, 1
 li a7, SYS_GRANT
 ecall
 beqz a0, 99f
 j cred_bad
99:
 beqz t0, 99f
 j cred_bad
99:
 beqz a2, 99f
 j cred_bad
99:
 beqz a3, 99f
 j cred_bad
99:
 beqz a4, 99f
 j cred_bad
99:
 beqz a5, 99f
 j cred_bad
99:
 beqz a6, 99f
 j cred_bad
99:
 beqz a7, 99f
 j cred_bad
99:
 li s9, 29
 li a0, 0
 li a1, 0
 li a2, 0
 li a3, 0
 li a4, 0
 li a5, 0
 li a6, 0
 mv a0, s1
 li a2, 999
 li a7, SYS_GETCRED
 ecall
 li t1, -4
 beq a0, t1, 99f
 j cred_bad
99:
 beqz t0, 99f
 j cred_bad
99:
 beqz a1, 99f
 j cred_bad
99:
 beqz a2, 99f
 j cred_bad
99:
 beqz a3, 99f
 j cred_bad
99:
 beqz a4, 99f
 j cred_bad
99:
 beqz a5, 99f
 j cred_bad
99:
 beqz a6, 99f
 j cred_bad
99:
 beqz a7, 99f
 j cred_bad
99:
 li s9, 30
 li a0, 0
 li a1, 0
 li a2, 0
 li a3, 0
 li a4, 0
 li a5, 0
 li a6, 0
 mv a0, s0
 li a1, 10
 mv a2, s3
 li a3, 1
 li a5, 1
 li a7, SYS_CALL
 ecall
 beqz a0, 99f
 j cred_bad
99:
 beqz t0, 99f
 j cred_bad
99:
 beqz a1, 99f
 j cred_bad
99:
 beqz a2, 99f
 j cred_bad
99:
 beqz a4, 99f
 j cred_bad
99:
 beqz a5, 99f
 j cred_bad
99:
 beqz a6, 99f
 j cred_bad
99:
 beqz a7, 99f
 j cred_bad
99:
 mv s7, a3
 li s9, 31
 li a0, 0
 li a1, 0
 li a2, 0
 li a3, 0
 li a4, 0
 li a5, 0
 li a6, 0
 mv a0, s7
 li a7, SYS_GETCRED
 ecall
 li t1, -3
 beq a0, t1, 99f
 j cred_bad
99:
 beqz t0, 99f
 j cred_bad
99:
 beqz a1, 99f
 j cred_bad
99:
 beqz a2, 99f
 j cred_bad
99:
 beqz a3, 99f
 j cred_bad
99:
 beqz a4, 99f
 j cred_bad
99:
 beqz a5, 99f
 j cred_bad
99:
 beqz a6, 99f
 j cred_bad
99:
 beqz a7, 99f
 j cred_bad
99:
 li t2, 576460752303423488
 sub s8, s7, t2
 li s9, 32
 li a0, 0
 li a1, 0
 li a2, 0
 li a3, 0
 li a4, 0
 li a5, 0
 li a6, 0
 li a1, 4294967296
 li a5, 2
 li a7, SYS_RECV
 ecall
 beqz a0, 99f
 j cred_bad
99:
 beqz t0, 99f
 j cred_bad
99:
 li t1, 3
 beq a1, t1, 99f
 j cred_bad
99:
 beqz a2, 99f
 j cred_bad
99:
 beqz a3, 99f
 j cred_bad
99:
 li t1, 4294967296
 beq a4, t1, 99f
 j cred_bad
99:
 beqz a5, 99f
 j cred_bad
99:
 beqz a6, 99f
 j cred_bad
99:
 beqz a7, 99f
 j cred_bad
99:
 li s9, 33
 li a0, 0
 li a1, 0
 li a2, 0
 li a3, 0
 li a4, 0
 li a5, 0
 li a6, 0
 mv a0, s8
 li a7, SYS_GETCRED
 ecall
 li t1, -3
 beq a0, t1, 99f
 j cred_bad
99:
 beqz t0, 99f
 j cred_bad
99:
 beqz a1, 99f
 j cred_bad
99:
 beqz a2, 99f
 j cred_bad
99:
 beqz a3, 99f
 j cred_bad
99:
 beqz a4, 99f
 j cred_bad
99:
 beqz a5, 99f
 j cred_bad
99:
 beqz a6, 99f
 j cred_bad
99:
 beqz a7, 99f
 j cred_bad
99:
 li t0, 262144
 ld t2, 104(t0)
 li s9, 34
 li a0, 0
 li a1, 0
 li a2, 0
 li a3, 0
 li a4, 0
 li a5, 0
 li a6, 0
 mv a0, t2
 mv a1, s8
 li a7, SYS_OBSERVE
 ecall
 li t1, -3
 beq a0, t1, 99f
 j cred_bad
99:
 beqz t0, 99f
 j cred_bad
99:
 beqz a1, 99f
 j cred_bad
99:
 beqz a2, 99f
 j cred_bad
99:
 beqz a3, 99f
 j cred_bad
99:
 beqz a4, 99f
 j cred_bad
99:
 beqz a5, 99f
 j cred_bad
99:
 beqz a6, 99f
 j cred_bad
99:
 beqz a7, 99f
 j cred_bad
99:
 fmv.x.d t0, f31
 li t1, 4660
 beq t0, t1, 99f
 j cred_bad
99:
 li s8, 42
 j cred_exit
cred_server:
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
 li s9, 35
 li a0, 0
 li a1, 0
 li a2, 0
 li a3, 0
 li a4, 0
 li a5, 0
 li a6, 0
 li a7, SYS_GETCRED
 ecall
 beqz a0, 99f
 j cred_bad
99:
 beqz t0, 99f
 j cred_bad
99:
 li t1, 50
 beq a1, t1, 99f
 j cred_bad
99:
 li t1, 60
 beq a2, t1, 99f
 j cred_bad
99:
 li t1, 70
 beq a3, t1, 99f
 j cred_bad
99:
 li t1, 80
 beq a4, t1, 99f
 j cred_bad
99:
 beqz a5, 99f
 j cred_bad
99:
 beqz a6, 99f
 j cred_bad
99:
 beqz a7, 99f
 j cred_bad
99:
 li s9, 36
 li a0, 0
 li a1, 0
 li a2, 0
 li a3, 0
 li a4, 0
 li a5, 0
 li a6, 0
 li a7, SYS_RECV
 ecall
 beqz a0, 99f
 j cred_bad
99:
 li t1, 1
 beq t0, t1, 99f
 j cred_bad
99:
 li t1, 1
 beq a1, t1, 99f
 j cred_bad
99:
 mv t1, s4
 beq a2, t1, 99f
 j cred_bad
99:
 li t1, 10
 beq a4, t1, 99f
 j cred_bad
99:
 mv t1, s3
 beq a5, t1, 99f
 j cred_bad
99:
 li t1, 1
 beq a6, t1, 99f
 j cred_bad
99:
 beqz a7, 99f
 j cred_bad
99:
 mv s7, a3
 li s9, 37
 li a0, 0
 li a1, 0
 li a2, 0
 li a3, 0
 li a4, 0
 li a5, 0
 li a6, 0
 mv a0, s7
 li a7, SYS_GETCRED
 ecall
 beqz a0, 99f
 j cred_bad
99:
 beqz t0, 99f
 j cred_bad
99:
 li t1, 10
 beq a1, t1, 99f
 j cred_bad
99:
 li t1, 20
 beq a2, t1, 99f
 j cred_bad
99:
 li t1, 30
 beq a3, t1, 99f
 j cred_bad
99:
 li t1, 40
 beq a4, t1, 99f
 j cred_bad
99:
 beqz a5, 99f
 j cred_bad
99:
 beqz a6, 99f
 j cred_bad
99:
 beqz a7, 99f
 j cred_bad
99:
 li s9, 38
 li a0, 0
 li a1, 0
 li a2, 0
 li a3, 0
 li a4, 0
 li a5, 0
 li a6, 0
 mv a0, s7
 mv a1, s3
 li a7, SYS_GETCRED
 ecall
 beqz a0, 99f
 j cred_bad
99:
 beqz t0, 99f
 j cred_bad
99:
 li t1, 110
 beq a1, t1, 99f
 j cred_bad
99:
 li t1, 120
 beq a2, t1, 99f
 j cred_bad
99:
 li t1, 130
 beq a3, t1, 99f
 j cred_bad
99:
 li t1, 140
 beq a4, t1, 99f
 j cred_bad
99:
 beqz a5, 99f
 j cred_bad
99:
 beqz a6, 99f
 j cred_bad
99:
 beqz a7, 99f
 j cred_bad
99:
 li s9, 39
 li a0, 0
 li a1, 0
 li a2, 0
 li a3, 0
 li a4, 0
 li a5, 0
 li a6, 0
 mv a0, s7
 mv a1, s3
 li a2, 1
 li a7, SYS_GETCRED
 ecall
 beqz a0, 99f
 j cred_bad
99:
 beqz t0, 99f
 j cred_bad
99:
 li t1, 110
 beq a1, t1, 99f
 j cred_bad
99:
 li t1, 120
 beq a2, t1, 99f
 j cred_bad
99:
 li t1, 130
 beq a3, t1, 99f
 j cred_bad
99:
 li t1, 140
 beq a4, t1, 99f
 j cred_bad
99:
 beqz a5, 99f
 j cred_bad
99:
 beqz a6, 99f
 j cred_bad
99:
 beqz a7, 99f
 j cred_bad
99:
 li t0, 262144
 ld t2, 104(t0)
 li s9, 40
 li a0, 0
 li a1, 0
 li a2, 0
 li a3, 0
 li a4, 0
 li a5, 0
 li a6, 0
 mv a0, t2
 mv a1, s7
 li a7, SYS_OBSERVE
 ecall
 beqz a0, 99f
 j cred_bad
99:
 beqz t0, 99f
 j cred_bad
99:
 li t1, 2
 beq a2, t1, 99f
 j cred_bad
99:
 beqz a3, 99f
 j cred_bad
99:
 beqz a4, 99f
 j cred_bad
99:
 beqz a5, 99f
 j cred_bad
99:
 beqz a6, 99f
 j cred_bad
99:
 beqz a7, 99f
 j cred_bad
99:
 mv s8, a1
 li t0, 262144
 ld t2, 104(t0)
 li s9, 41
 li a0, 0
 li a1, 0
 li a2, 0
 li a3, 0
 li a4, 0
 li a5, 0
 li a6, 0
 mv a0, t2
 mv a1, s7
 li a7, SYS_OBSERVE
 ecall
 beqz a0, 99f
 j cred_bad
99:
 beqz t0, 99f
 j cred_bad
99:
 mv t1, s8
 beq a1, t1, 99f
 j cred_bad
99:
 li t1, 2
 beq a2, t1, 99f
 j cred_bad
99:
 beqz a3, 99f
 j cred_bad
99:
 beqz a4, 99f
 j cred_bad
99:
 beqz a5, 99f
 j cred_bad
99:
 beqz a6, 99f
 j cred_bad
99:
 beqz a7, 99f
 j cred_bad
99:
 li t0, 262144
 ld t2, 104(t0)
 li s9, 42
 li a0, 0
 li a1, 0
 li a2, 0
 li a3, 0
 li a4, 0
 li a5, 0
 li a6, 0
 mv a0, t2
 mv a1, s7
 li a2, 1
 mv a3, s3
 li a7, SYS_OBSERVE
 ecall
 beqz a0, 99f
 j cred_bad
99:
 beqz t0, 99f
 j cred_bad
99:
 li t1, 1
 beq a2, t1, 99f
 j cred_bad
99:
 beqz a3, 99f
 j cred_bad
99:
 beqz a4, 99f
 j cred_bad
99:
 beqz a5, 99f
 j cred_bad
99:
 beqz a6, 99f
 j cred_bad
99:
 beqz a7, 99f
 j cred_bad
99:
 li t0, 262272
 sd a1, 0(t0)
 li t0, 262144
 ld t2, 104(t0)
 li s9, 43
 li a0, 0
 li a1, 0
 li a2, 0
 li a3, 0
 li a4, 0
 li a5, 0
 li a6, 0
 mv a0, t2
 mv a1, s7
 mv a3, s4
 li a7, SYS_OBSERVE
 ecall
 li t1, -1
 beq a0, t1, 99f
 j cred_bad
99:
 beqz t0, 99f
 j cred_bad
99:
 beqz a1, 99f
 j cred_bad
99:
 beqz a2, 99f
 j cred_bad
99:
 beqz a3, 99f
 j cred_bad
99:
 beqz a4, 99f
 j cred_bad
99:
 beqz a5, 99f
 j cred_bad
99:
 beqz a6, 99f
 j cred_bad
99:
 beqz a7, 99f
 j cred_bad
99:
 li t0, 262144
 ld t2, 104(t0)
 li s9, 44
 li a0, 0
 li a1, 0
 li a2, 0
 li a3, 0
 li a4, 0
 li a5, 0
 li a6, 0
 mv a0, t2
 mv a1, s1
 li a7, SYS_OBSERVE
 ecall
 li t1, -3
 beq a0, t1, 99f
 j cred_bad
99:
 beqz t0, 99f
 j cred_bad
99:
 beqz a1, 99f
 j cred_bad
99:
 beqz a2, 99f
 j cred_bad
99:
 beqz a3, 99f
 j cred_bad
99:
 beqz a4, 99f
 j cred_bad
99:
 beqz a5, 99f
 j cred_bad
99:
 beqz a6, 99f
 j cred_bad
99:
 beqz a7, 99f
 j cred_bad
99:
 li t0, 262144
 ld t2, 104(t0)
 li s9, 45
 li a0, 0
 li a1, 0
 li a2, 0
 li a3, 0
 li a4, 0
 li a5, 0
 li a6, 0
 mv a0, t2
 mv a1, s7
 li a2, 3
 li a7, SYS_OBSERVE
 ecall
 li t1, -3
 beq a0, t1, 99f
 j cred_bad
99:
 beqz t0, 99f
 j cred_bad
99:
 beqz a1, 99f
 j cred_bad
99:
 beqz a2, 99f
 j cred_bad
99:
 beqz a3, 99f
 j cred_bad
99:
 beqz a4, 99f
 j cred_bad
99:
 beqz a5, 99f
 j cred_bad
99:
 beqz a6, 99f
 j cred_bad
99:
 beqz a7, 99f
 j cred_bad
99:
 li t0, 262144
 ld t2, 104(t0)
 li s9, 46
 li a0, 0
 li a1, 0
 li a2, 0
 li a3, 0
 li a4, 0
 li a5, 0
 li a6, 0
 mv a0, t2
 mv a1, s7
 li a2, 2
 mv a3, s4
 li a7, SYS_OBSERVE
 ecall
 li t1, -1
 beq a0, t1, 99f
 j cred_bad
99:
 beqz t0, 99f
 j cred_bad
99:
 beqz a1, 99f
 j cred_bad
99:
 beqz a2, 99f
 j cred_bad
99:
 beqz a3, 99f
 j cred_bad
99:
 beqz a4, 99f
 j cred_bad
99:
 beqz a5, 99f
 j cred_bad
99:
 beqz a6, 99f
 j cred_bad
99:
 beqz a7, 99f
 j cred_bad
99:
 li t0, 262144
 ld t2, 104(t0)
 li s9, 47
 li a0, 0
 li a1, 0
 li a2, 0
 li a3, 0
 li a4, 0
 li a5, 0
 li a6, 0
 mv a0, t2
 mv a1, s7
 li a2, 1
 mv a3, s5
 li a7, SYS_OBSERVE
 ecall
 li t1, -4
 beq a0, t1, 99f
 j cred_bad
99:
 beqz t0, 99f
 j cred_bad
99:
 beqz a1, 99f
 j cred_bad
99:
 beqz a2, 99f
 j cred_bad
99:
 beqz a3, 99f
 j cred_bad
99:
 beqz a4, 99f
 j cred_bad
99:
 beqz a5, 99f
 j cred_bad
99:
 beqz a6, 99f
 j cred_bad
99:
 beqz a7, 99f
 j cred_bad
99:
 li s9, 48
 li a0, 0
 li a1, 0
 li a2, 0
 li a3, 0
 li a4, 0
 li a5, 0
 li a6, 0
 mv a0, s7
 li a2, 1
 li a7, SYS_GETCRED
 ecall
 li t1, -1
 beq a0, t1, 99f
 j cred_bad
99:
 beqz t0, 99f
 j cred_bad
99:
 beqz a1, 99f
 j cred_bad
99:
 beqz a2, 99f
 j cred_bad
99:
 beqz a3, 99f
 j cred_bad
99:
 beqz a4, 99f
 j cred_bad
99:
 beqz a5, 99f
 j cred_bad
99:
 beqz a6, 99f
 j cred_bad
99:
 beqz a7, 99f
 j cred_bad
99:
 li s9, 49
 li a0, 0
 li a1, 0
 li a2, 0
 li a3, 0
 li a4, 0
 li a5, 0
 li a6, 0
 mv a0, s7
 mv a1, s4
 li a7, SYS_GETCRED
 ecall
 li t1, -1
 beq a0, t1, 99f
 j cred_bad
99:
 beqz t0, 99f
 j cred_bad
99:
 beqz a1, 99f
 j cred_bad
99:
 beqz a2, 99f
 j cred_bad
99:
 beqz a3, 99f
 j cred_bad
99:
 beqz a4, 99f
 j cred_bad
99:
 beqz a5, 99f
 j cred_bad
99:
 beqz a6, 99f
 j cred_bad
99:
 beqz a7, 99f
 j cred_bad
99:
 li s9, 50
 li a0, 0
 li a1, 0
 li a2, 0
 li a3, 0
 li a4, 0
 li a5, 0
 li a6, 0
 mv a0, s7
 mv a1, s4
 li a2, 1
 li a7, SYS_GETCRED
 ecall
 li t1, -4
 beq a0, t1, 99f
 j cred_bad
99:
 beqz t0, 99f
 j cred_bad
99:
 beqz a1, 99f
 j cred_bad
99:
 beqz a2, 99f
 j cred_bad
99:
 beqz a3, 99f
 j cred_bad
99:
 beqz a4, 99f
 j cred_bad
99:
 beqz a5, 99f
 j cred_bad
99:
 beqz a6, 99f
 j cred_bad
99:
 beqz a7, 99f
 j cred_bad
99:
 li s9, 51
 li a0, 0
 li a1, 0
 li a2, 0
 li a3, 0
 li a4, 0
 li a5, 0
 li a6, 0
 mv a0, s7
 mv a1, s5
 li a7, SYS_GETCRED
 ecall
 li t1, -4
 beq a0, t1, 99f
 j cred_bad
99:
 beqz t0, 99f
 j cred_bad
99:
 beqz a1, 99f
 j cred_bad
99:
 beqz a2, 99f
 j cred_bad
99:
 beqz a3, 99f
 j cred_bad
99:
 beqz a4, 99f
 j cred_bad
99:
 beqz a5, 99f
 j cred_bad
99:
 beqz a6, 99f
 j cred_bad
99:
 beqz a7, 99f
 j cred_bad
99:
 li s9, 52
 li a0, 0
 li a1, 0
 li a2, 0
 li a3, 0
 li a4, 0
 li a5, 0
 li a6, 0
 mv a0, s7
 li a7, SYS_DEFER
 ecall
 beqz a0, 99f
 j cred_bad
99:
 beqz t0, 99f
 j cred_bad
99:
 beqz a1, 99f
 j cred_bad
99:
 beqz a2, 99f
 j cred_bad
99:
 beqz a3, 99f
 j cred_bad
99:
 beqz a4, 99f
 j cred_bad
99:
 beqz a5, 99f
 j cred_bad
99:
 beqz a6, 99f
 j cred_bad
99:
 beqz a7, 99f
 j cred_bad
99:
 li s9, 53
 li a0, 0
 li a1, 0
 li a2, 0
 li a3, 0
 li a4, 0
 li a5, 0
 li a6, 0
 mv a0, s6
 li a1, 4294967296
 li a7, SYS_NOTIFY
 ecall
 beqz a0, 99f
 j cred_bad
99:
 beqz t0, 99f
 j cred_bad
99:
 beqz a1, 99f
 j cred_bad
99:
 beqz a2, 99f
 j cred_bad
99:
 beqz a3, 99f
 j cred_bad
99:
 beqz a4, 99f
 j cred_bad
99:
 beqz a5, 99f
 j cred_bad
99:
 beqz a6, 99f
 j cred_bad
99:
 beqz a7, 99f
 j cred_bad
99:
 li s9, 54
 li a0, 0
 li a1, 0
 li a2, 0
 li a3, 0
 li a4, 0
 li a5, 0
 li a6, 0
 li a1, 8589934592
 li a5, 2
 li a7, SYS_RECV
 ecall
 beqz a0, 99f
 j cred_bad
99:
 beqz t0, 99f
 j cred_bad
99:
 li t1, 3
 beq a1, t1, 99f
 j cred_bad
99:
 beqz a2, 99f
 j cred_bad
99:
 beqz a3, 99f
 j cred_bad
99:
 li t1, 8589934592
 beq a4, t1, 99f
 j cred_bad
99:
 beqz a5, 99f
 j cred_bad
99:
 beqz a6, 99f
 j cred_bad
99:
 beqz a7, 99f
 j cred_bad
99:
 li s9, 55
 li a0, 0
 li a1, 0
 li a2, 0
 li a3, 0
 li a4, 0
 li a5, 0
 li a6, 0
 mv a0, s7
 li a7, SYS_GETCRED
 ecall
 beqz a0, 99f
 j cred_bad
99:
 beqz t0, 99f
 j cred_bad
99:
 li t1, 10
 beq a1, t1, 99f
 j cred_bad
99:
 li t1, 20
 beq a2, t1, 99f
 j cred_bad
99:
 li t1, 30
 beq a3, t1, 99f
 j cred_bad
99:
 li t1, 40
 beq a4, t1, 99f
 j cred_bad
99:
 beqz a5, 99f
 j cred_bad
99:
 beqz a6, 99f
 j cred_bad
99:
 beqz a7, 99f
 j cred_bad
99:
 li s9, 56
 li a0, 0
 li a1, 0
 li a2, 0
 li a3, 0
 li a4, 0
 li a5, 0
 li a6, 0
 mv a0, s7
 mv a1, s3
 li a7, SYS_GETCRED
 ecall
 beqz a0, 99f
 j cred_bad
99:
 beqz t0, 99f
 j cred_bad
99:
 li t1, 110
 beq a1, t1, 99f
 j cred_bad
99:
 li t1, 120
 beq a2, t1, 99f
 j cred_bad
99:
 li t1, 130
 beq a3, t1, 99f
 j cred_bad
99:
 li t1, 140
 beq a4, t1, 99f
 j cred_bad
99:
 beqz a5, 99f
 j cred_bad
99:
 beqz a6, 99f
 j cred_bad
99:
 beqz a7, 99f
 j cred_bad
99:
 li s9, 57
 li a0, 0
 li a1, 0
 li a2, 0
 li a3, 0
 li a4, 0
 li a5, 0
 li a6, 0
 mv a0, s1
 li a7, SYS_GETCRED
 ecall
 beqz a0, 99f
 j cred_bad
99:
 beqz t0, 99f
 j cred_bad
99:
 li t1, 10
 beq a1, t1, 99f
 j cred_bad
99:
 li t1, 20
 beq a2, t1, 99f
 j cred_bad
99:
 li t1, 30
 beq a3, t1, 99f
 j cred_bad
99:
 li t1, 40
 beq a4, t1, 99f
 j cred_bad
99:
 beqz a5, 99f
 j cred_bad
99:
 beqz a6, 99f
 j cred_bad
99:
 beqz a7, 99f
 j cred_bad
99:
 li s9, 58
 li a0, 0
 li a1, 0
 li a2, 0
 li a3, 0
 li a4, 0
 li a5, 0
 li a6, 0
 mv a0, s1
 mv a1, s4
 li a2, 1
 li a7, SYS_GETCRED
 ecall
 beqz a0, 99f
 j cred_bad
99:
 beqz t0, 99f
 j cred_bad
99:
 li t1, 10
 beq a1, t1, 99f
 j cred_bad
99:
 li t1, 20
 beq a2, t1, 99f
 j cred_bad
99:
 li t1, 30
 beq a3, t1, 99f
 j cred_bad
99:
 li t1, 40
 beq a4, t1, 99f
 j cred_bad
99:
 beqz a5, 99f
 j cred_bad
99:
 beqz a6, 99f
 j cred_bad
99:
 beqz a7, 99f
 j cred_bad
99:
 li s9, 59
 li a0, 0
 li a1, 0
 li a2, 0
 li a3, 0
 li a4, 0
 li a5, 0
 li a6, 0
 li a1, 16
 li a5, 3
 li a7, SYS_RECV
 ecall
 beqz a0, 99f
 j cred_bad
99:
 beqz t0, 99f
 j cred_bad
99:
 li t1, 3
 beq a1, t1, 99f
 j cred_bad
99:
 beqz a2, 99f
 j cred_bad
99:
 beqz a3, 99f
 j cred_bad
99:
 li t1, 16
 beq a4, t1, 99f
 j cred_bad
99:
 beqz a5, 99f
 j cred_bad
99:
 beqz a6, 99f
 j cred_bad
99:
 beqz a7, 99f
 j cred_bad
99:
 li s9, 60
 li a0, 0
 li a1, 0
 li a2, 0
 li a3, 0
 li a4, 0
 li a5, 0
 li a6, 0
 mv a0, s8
 li a7, SYS_OBSREAD
 ecall
 beqz a0, 99f
 j cred_bad
99:
 beqz t0, 99f
 j cred_bad
99:
 li t1, 3
 beq a1, t1, 99f
 j cred_bad
99:
 li t1, 1
 beq a2, t1, 99f
 j cred_bad
99:
 mv t1, s4
 beq a3, t1, 99f
 j cred_bad
99:
 li t1, 1
 beq a4, t1, 99f
 j cred_bad
99:
 beqz a5, 99f
 j cred_bad
99:
 beqz a6, 99f
 j cred_bad
99:
 beqz a7, 99f
 j cred_bad
99:
 li t0, 262144
 ld t2, 104(t0)
 li s9, 61
 li a0, 0
 li a1, 0
 li a2, 0
 li a3, 0
 li a4, 0
 li a5, 0
 li a6, 0
 mv a0, t2
 mv a1, s7
 li a7, SYS_OBSERVE
 ecall
 beqz a0, 99f
 j cred_bad
99:
 beqz t0, 99f
 j cred_bad
99:
 beqz a1, 99f
 j cred_bad
99:
 li t1, 3
 beq a2, t1, 99f
 j cred_bad
99:
 li t1, 1
 beq a3, t1, 99f
 j cred_bad
99:
 beqz a4, 99f
 j cred_bad
99:
 beqz a5, 99f
 j cred_bad
99:
 beqz a6, 99f
 j cred_bad
99:
 beqz a7, 99f
 j cred_bad
99:
 li t0, 262144
 ld t2, 104(t0)
 li s9, 62
 li a0, 0
 li a1, 0
 li a2, 0
 li a3, 0
 li a4, 0
 li a5, 0
 li a6, 0
 mv a0, t2
 mv a1, s7
 li a2, 1
 mv a3, s3
 li a7, SYS_OBSERVE
 ecall
 beqz a0, 99f
 j cred_bad
99:
 beqz t0, 99f
 j cred_bad
99:
 beqz a1, 99f
 j cred_bad
99:
 li t1, 3
 beq a2, t1, 99f
 j cred_bad
99:
 li t1, 2
 beq a3, t1, 99f
 j cred_bad
99:
 beqz a4, 99f
 j cred_bad
99:
 beqz a5, 99f
 j cred_bad
99:
 beqz a6, 99f
 j cred_bad
99:
 beqz a7, 99f
 j cred_bad
99:
 li t0, 262272
 ld t2, 0(t0)
 li s9, 63
 li a0, 0
 li a1, 0
 li a2, 0
 li a3, 0
 li a4, 0
 li a5, 0
 li a6, 0
 mv a0, t2
 li a7, SYS_OBSACK
 ecall
 beqz a0, 99f
 j cred_bad
99:
 beqz t0, 99f
 j cred_bad
99:
 beqz a1, 99f
 j cred_bad
99:
 beqz a2, 99f
 j cred_bad
99:
 beqz a3, 99f
 j cred_bad
99:
 beqz a4, 99f
 j cred_bad
99:
 beqz a5, 99f
 j cred_bad
99:
 beqz a6, 99f
 j cred_bad
99:
 beqz a7, 99f
 j cred_bad
99:
 li t0, 262208
 li t1, 1
 sd t1, 0(t0)
 li s9, 64
 li a0, 0
 li a1, 0
 li a2, 0
 li a3, 0
 li a4, 0
 li a5, 0
 li a6, 0
 li a7, SYS_YIELD
 ecall
 beqz a0, 99f
 j cred_bad
99:
 beqz t0, 99f
 j cred_bad
99:
 beqz a1, 99f
 j cred_bad
99:
 beqz a2, 99f
 j cred_bad
99:
 beqz a3, 99f
 j cred_bad
99:
 beqz a4, 99f
 j cred_bad
99:
 beqz a5, 99f
 j cred_bad
99:
 beqz a6, 99f
 j cred_bad
99:
 beqz a7, 99f
 j cred_bad
99:
 li s9, 65
 li a0, 0
 li a1, 0
 li a2, 0
 li a3, 0
 li a4, 0
 li a5, 0
 li a6, 0
 mv a0, s7
 mv a1, s3
 li a7, SYS_GETCRED
 ecall
 li t1, -4
 beq a0, t1, 99f
 j cred_bad
99:
 beqz t0, 99f
 j cred_bad
99:
 beqz a1, 99f
 j cred_bad
99:
 beqz a2, 99f
 j cred_bad
99:
 beqz a3, 99f
 j cred_bad
99:
 beqz a4, 99f
 j cred_bad
99:
 beqz a5, 99f
 j cred_bad
99:
 beqz a6, 99f
 j cred_bad
99:
 beqz a7, 99f
 j cred_bad
99:
 li s9, 66
 li a0, 0
 li a1, 0
 li a2, 0
 li a3, 0
 li a4, 0
 li a5, 0
 li a6, 0
 mv a0, s7
 li a7, SYS_GETCRED
 ecall
 beqz a0, 99f
 j cred_bad
99:
 beqz t0, 99f
 j cred_bad
99:
 li t1, 10
 beq a1, t1, 99f
 j cred_bad
99:
 li t1, 20
 beq a2, t1, 99f
 j cred_bad
99:
 li t1, 30
 beq a3, t1, 99f
 j cred_bad
99:
 li t1, 40
 beq a4, t1, 99f
 j cred_bad
99:
 beqz a5, 99f
 j cred_bad
99:
 beqz a6, 99f
 j cred_bad
99:
 beqz a7, 99f
 j cred_bad
99:
 li t0, 262144
 ld t2, 104(t0)
 li s9, 67
 li a0, 0
 li a1, 0
 li a2, 0
 li a3, 0
 li a4, 0
 li a5, 0
 li a6, 0
 mv a0, t2
 mv a1, s7
 li a2, 1
 mv a3, s3
 li a7, SYS_OBSERVE
 ecall
 li t1, -4
 beq a0, t1, 99f
 j cred_bad
99:
 beqz t0, 99f
 j cred_bad
99:
 beqz a1, 99f
 j cred_bad
99:
 beqz a2, 99f
 j cred_bad
99:
 beqz a3, 99f
 j cred_bad
99:
 beqz a4, 99f
 j cred_bad
99:
 beqz a5, 99f
 j cred_bad
99:
 beqz a6, 99f
 j cred_bad
99:
 beqz a7, 99f
 j cred_bad
99:
 li s9, 68
 li a0, 0
 li a1, 0
 li a2, 0
 li a3, 0
 li a4, 0
 li a5, 0
 li a6, 0
 mv a0, s7
 li a7, SYS_RQFINISH
 ecall
 beqz a0, 99f
 j cred_bad
99:
 beqz t0, 99f
 j cred_bad
99:
 beqz a1, 99f
 j cred_bad
99:
 beqz a2, 99f
 j cred_bad
99:
 beqz a3, 99f
 j cred_bad
99:
 beqz a4, 99f
 j cred_bad
99:
 beqz a5, 99f
 j cred_bad
99:
 beqz a6, 99f
 j cred_bad
99:
 beqz a7, 99f
 j cred_bad
99:
 li s9, 69
 li a0, 0
 li a1, 0
 li a2, 0
 li a3, 0
 li a4, 0
 li a5, 0
 li a6, 0
 mv a0, s8
 li a7, SYS_OBSREAD
 ecall
 beqz a0, 99f
 j cred_bad
99:
 beqz t0, 99f
 j cred_bad
99:
 li t1, 3
 beq a1, t1, 99f
 j cred_bad
99:
 li t1, 1
 beq a2, t1, 99f
 j cred_bad
99:
 mv t1, s4
 beq a3, t1, 99f
 j cred_bad
99:
 li t1, 1
 beq a4, t1, 99f
 j cred_bad
99:
 beqz a5, 99f
 j cred_bad
99:
 beqz a6, 99f
 j cred_bad
99:
 beqz a7, 99f
 j cred_bad
99:
 li s9, 70
 li a0, 0
 li a1, 0
 li a2, 0
 li a3, 0
 li a4, 0
 li a5, 0
 li a6, 0
 mv a0, s8
 li a7, SYS_OBSACK
 ecall
 beqz a0, 99f
 j cred_bad
99:
 beqz t0, 99f
 j cred_bad
99:
 beqz a1, 99f
 j cred_bad
99:
 beqz a2, 99f
 j cred_bad
99:
 beqz a3, 99f
 j cred_bad
99:
 beqz a4, 99f
 j cred_bad
99:
 beqz a5, 99f
 j cred_bad
99:
 beqz a6, 99f
 j cred_bad
99:
 beqz a7, 99f
 j cred_bad
99:
 li s9, 71
 li a0, 0
 li a1, 0
 li a2, 0
 li a3, 0
 li a4, 0
 li a5, 0
 li a6, 0
 mv a0, s8
 li a7, SYS_OBSREAD
 ecall
 li t1, -4
 beq a0, t1, 99f
 j cred_bad
99:
 beqz t0, 99f
 j cred_bad
99:
 beqz a1, 99f
 j cred_bad
99:
 beqz a2, 99f
 j cred_bad
99:
 beqz a3, 99f
 j cred_bad
99:
 beqz a4, 99f
 j cred_bad
99:
 beqz a5, 99f
 j cred_bad
99:
 beqz a6, 99f
 j cred_bad
99:
 beqz a7, 99f
 j cred_bad
99:
 li s9, 72
 li a0, 0
 li a1, 0
 li a2, 0
 li a3, 0
 li a4, 0
 li a5, 0
 li a6, 0
 li a7, SYS_OBSNEXT
 ecall
 beqz a0, 99f
 j cred_bad
99:
 beqz t0, 99f
 j cred_bad
99:
 beqz a1, 99f
 j cred_bad
99:
 beqz a2, 99f
 j cred_bad
99:
 beqz a3, 99f
 j cred_bad
99:
 beqz a4, 99f
 j cred_bad
99:
 beqz a5, 99f
 j cred_bad
99:
 beqz a6, 99f
 j cred_bad
99:
 beqz a7, 99f
 j cred_bad
99:
 li s9, 73
 li a0, 0
 li a1, 0
 li a2, 0
 li a3, 0
 li a4, 0
 li a5, 0
 li a6, 0
 mv a0, s7
 li a7, SYS_GETCRED
 ecall
 li t1, -3
 beq a0, t1, 99f
 j cred_bad
99:
 beqz t0, 99f
 j cred_bad
99:
 beqz a1, 99f
 j cred_bad
99:
 beqz a2, 99f
 j cred_bad
99:
 beqz a3, 99f
 j cred_bad
99:
 beqz a4, 99f
 j cred_bad
99:
 beqz a5, 99f
 j cred_bad
99:
 beqz a6, 99f
 j cred_bad
99:
 beqz a7, 99f
 j cred_bad
99:
 li s9, 74
 li a0, 0
 li a1, 0
 li a2, 0
 li a3, 0
 li a4, 0
 li a5, 0
 li a6, 0
 mv a0, s0
 li a7, SYS_FREE
 ecall
 beqz a0, 99f
 j cred_bad
99:
 beqz t0, 99f
 j cred_bad
99:
 beqz a1, 99f
 j cred_bad
99:
 beqz a2, 99f
 j cred_bad
99:
 beqz a3, 99f
 j cred_bad
99:
 beqz a4, 99f
 j cred_bad
99:
 beqz a5, 99f
 j cred_bad
99:
 beqz a6, 99f
 j cred_bad
99:
 beqz a7, 99f
 j cred_bad
99:
 li s9, 75
 li a0, 0
 li a1, 0
 li a2, 0
 li a3, 0
 li a4, 0
 li a5, 0
 li a6, 0
 mv a0, s6
 li a7, SYS_FREE
 ecall
 beqz a0, 99f
 j cred_bad
99:
 beqz t0, 99f
 j cred_bad
99:
 beqz a1, 99f
 j cred_bad
99:
 beqz a2, 99f
 j cred_bad
99:
 beqz a3, 99f
 j cred_bad
99:
 beqz a4, 99f
 j cred_bad
99:
 beqz a5, 99f
 j cred_bad
99:
 beqz a6, 99f
 j cred_bad
99:
 beqz a7, 99f
 j cred_bad
99:
 li s9, 76
 li a0, 0
 li a1, 0
 li a2, 0
 li a3, 0
 li a4, 0
 li a5, 0
 li a6, 0
 mv a0, s1
 li a7, SYS_GETCRED
 ecall
 beqz a0, 99f
 j cred_bad
99:
 beqz t0, 99f
 j cred_bad
99:
 li t1, 10
 beq a1, t1, 99f
 j cred_bad
99:
 li t1, 20
 beq a2, t1, 99f
 j cred_bad
99:
 li t1, 30
 beq a3, t1, 99f
 j cred_bad
99:
 li t1, 40
 beq a4, t1, 99f
 j cred_bad
99:
 beqz a5, 99f
 j cred_bad
99:
 beqz a6, 99f
 j cred_bad
99:
 beqz a7, 99f
 j cred_bad
99:
 li s9, 77
 li a0, 0
 li a1, 0
 li a2, 0
 li a3, 0
 li a4, 0
 li a5, 0
 li a6, 0
 mv a0, s1
 li a2, 1
 li a7, SYS_GETCRED
 ecall
 li t1, -4
 beq a0, t1, 99f
 j cred_bad
99:
 beqz t0, 99f
 j cred_bad
99:
 beqz a1, 99f
 j cred_bad
99:
 beqz a2, 99f
 j cred_bad
99:
 beqz a3, 99f
 j cred_bad
99:
 beqz a4, 99f
 j cred_bad
99:
 beqz a5, 99f
 j cred_bad
99:
 beqz a6, 99f
 j cred_bad
99:
 beqz a7, 99f
 j cred_bad
99:
 fmv.x.d t0, f31
 li t1, 4660
 beq t0, t1, 99f
 j cred_bad
99:
 li s8, 42
 j cred_exit
cred_replacement:
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
 li s9, 78
 li a0, 0
 li a1, 0
 li a2, 0
 li a3, 0
 li a4, 0
 li a5, 0
 li a6, 0
 li a7, SYS_GETCRED
 ecall
 beqz a0, 99f
 j cred_bad
99:
 beqz t0, 99f
 j cred_bad
99:
 li t1, 210
 beq a1, t1, 99f
 j cred_bad
99:
 li t1, 220
 beq a2, t1, 99f
 j cred_bad
99:
 li t1, 230
 beq a3, t1, 99f
 j cred_bad
99:
 li t1, 240
 beq a4, t1, 99f
 j cred_bad
99:
 beqz a5, 99f
 j cred_bad
99:
 beqz a6, 99f
 j cred_bad
99:
 beqz a7, 99f
 j cred_bad
99:
 li s9, 79
 li a0, 0
 li a1, 0
 li a2, 0
 li a3, 0
 li a4, 0
 li a5, 0
 li a6, 0
 mv a0, s6
 li a1, 8589934592
 li a7, SYS_NOTIFY
 ecall
 beqz a0, 99f
 j cred_bad
99:
 beqz t0, 99f
 j cred_bad
99:
 beqz a1, 99f
 j cred_bad
99:
 beqz a2, 99f
 j cred_bad
99:
 beqz a3, 99f
 j cred_bad
99:
 beqz a4, 99f
 j cred_bad
99:
 beqz a5, 99f
 j cred_bad
99:
 beqz a6, 99f
 j cred_bad
99:
 beqz a7, 99f
 j cred_bad
99:
 li s8, 42
 j cred_exit
cred_bad:
 addi s8, s9, 100
cred_exit:
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
 j cred_bad
cred_end:
