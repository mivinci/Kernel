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
 bnez a0, cred_bad
 bnez t0, cred_bad
 li t1, 10
 bne a1, t1, cred_bad
 li t1, 20
 bne a2, t1, cred_bad
 li t1, 30
 bne a3, t1, cred_bad
 li t1, 40
 bne a4, t1, cred_bad
 bnez a5, cred_bad
 bnez a6, cred_bad
 bnez a7, cred_bad
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
 bnez a0, cred_bad
 bnez t0, cred_bad
 li t1, 2
 bne a1, t1, cred_bad
 bnez a2, cred_bad
 bnez a3, cred_bad
 bnez a4, cred_bad
 bnez a5, cred_bad
 bnez a6, cred_bad
 bnez a7, cred_bad
 li t0, 263168
 ld t2, 0(t0)
 li t1, 5
 bne t2, t1, cred_bad
 li t0, 263168
 ld t2, 8(t0)
 mv t1, s3
 bne t2, t1, cred_bad
 li t0, 263168
 ld t2, 16(t0)
 li t1, 1
 bne t2, t1, cred_bad
 li t0, 263168
 ld t2, 40(t0)
 li t1, 2
 bne t2, t1, cred_bad
 li t0, 262144
 ld s7, 88(t0)
 li t0, 263168
 ld t2, 32(t0)
 mv t1, s7
 bne t2, t1, cred_bad
 li t0, 263360
 ld t2, 0(t0)
 li t1, 5
 bne t2, t1, cred_bad
 li t0, 263360
 ld t2, 8(t0)
 mv t1, s3
 bne t2, t1, cred_bad
 li t0, 263360
 ld t2, 16(t0)
 li t1, 1
 bne t2, t1, cred_bad
 li t0, 263360
 ld t2, 40(t0)
 li t1, 3
 bne t2, t1, cred_bad
 li t0, 262144
 ld s7, 96(t0)
 li t0, 263360
 ld t2, 32(t0)
 mv t1, s7
 bne t2, t1, cred_bad
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
 bne a0, t1, cred_bad
 bnez t0, cred_bad
 bnez a1, cred_bad
 bnez a2, cred_bad
 bnez a3, cred_bad
 bnez a4, cred_bad
 bnez a5, cred_bad
 bnez a6, cred_bad
 bnez a7, cred_bad
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
 bne a0, t1, cred_bad
 bnez t0, cred_bad
 bnez a1, cred_bad
 bnez a2, cred_bad
 bnez a3, cred_bad
 bnez a4, cred_bad
 bnez a5, cred_bad
 bnez a6, cred_bad
 bnez a7, cred_bad
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
 bne a0, t1, cred_bad
 bnez t0, cred_bad
 bnez a1, cred_bad
 bnez a2, cred_bad
 bnez a3, cred_bad
 bnez a4, cred_bad
 bnez a5, cred_bad
 bnez a6, cred_bad
 bnez a7, cred_bad
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
 bne a0, t1, cred_bad
 bnez t0, cred_bad
 bnez a1, cred_bad
 bnez a2, cred_bad
 bnez a3, cred_bad
 bnez a4, cred_bad
 bnez a5, cred_bad
 bnez a6, cred_bad
 bnez a7, cred_bad
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
 bne a0, t1, cred_bad
 bnez t0, cred_bad
 bnez a1, cred_bad
 bnez a2, cred_bad
 bnez a3, cred_bad
 bnez a4, cred_bad
 bnez a5, cred_bad
 bnez a6, cred_bad
 bnez a7, cred_bad
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
 bne a0, t1, cred_bad
 bnez t0, cred_bad
 bnez a1, cred_bad
 bnez a2, cred_bad
 bnez a3, cred_bad
 bnez a4, cred_bad
 bnez a5, cred_bad
 bnez a6, cred_bad
 bnez a7, cred_bad
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
 li t1, -18
 bne a0, t1, cred_bad
 bnez t0, cred_bad
 bnez a1, cred_bad
 bnez a2, cred_bad
 bnez a3, cred_bad
 bnez a4, cred_bad
 bnez a5, cred_bad
 bnez a6, cred_bad
 bnez a7, cred_bad
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
 bne a0, t1, cred_bad
 bnez t0, cred_bad
 bnez a1, cred_bad
 bnez a2, cred_bad
 bnez a3, cred_bad
 bnez a4, cred_bad
 bnez a5, cred_bad
 bnez a6, cred_bad
 bnez a7, cred_bad
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
 bne a0, t1, cred_bad
 bnez t0, cred_bad
 bnez a1, cred_bad
 bnez a2, cred_bad
 bnez a3, cred_bad
 bnez a4, cred_bad
 bnez a5, cred_bad
 bnez a6, cred_bad
 bnez a7, cred_bad
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
 bne a0, t1, cred_bad
 bnez t0, cred_bad
 bnez a1, cred_bad
 bnez a2, cred_bad
 bnez a3, cred_bad
 bnez a4, cred_bad
 bnez a5, cred_bad
 bnez a6, cred_bad
 bnez a7, cred_bad
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
 bne a0, t1, cred_bad
 bnez t0, cred_bad
 bnez a1, cred_bad
 bnez a2, cred_bad
 bnez a3, cred_bad
 bnez a4, cred_bad
 bnez a5, cred_bad
 bnez a6, cred_bad
 bnez a7, cred_bad
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
 bne a0, t1, cred_bad
 bnez t0, cred_bad
 bnez a1, cred_bad
 bnez a2, cred_bad
 bnez a3, cred_bad
 bnez a4, cred_bad
 bnez a5, cred_bad
 bnez a6, cred_bad
 bnez a7, cred_bad
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
 bne a0, t1, cred_bad
 bnez t0, cred_bad
 bnez a1, cred_bad
 bnez a2, cred_bad
 bnez a3, cred_bad
 bnez a4, cred_bad
 bnez a5, cred_bad
 bnez a6, cred_bad
 bnez a7, cred_bad
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
 bnez a0, cred_bad
 bnez t0, cred_bad
 li t1, 50
 bne a1, t1, cred_bad
 li t1, 60
 bne a2, t1, cred_bad
 li t1, 70
 bne a3, t1, cred_bad
 li t1, 80
 bne a4, t1, cred_bad
 bnez a5, cred_bad
 bnez a6, cred_bad
 bnez a7, cred_bad
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
 bnez a0, cred_bad
 bnez t0, cred_bad
 li t1, 50
 bne a1, t1, cred_bad
 li t1, 60
 bne a2, t1, cred_bad
 li t1, 70
 bne a3, t1, cred_bad
 li t1, 80
 bne a4, t1, cred_bad
 bnez a5, cred_bad
 bnez a6, cred_bad
 bnez a7, cred_bad
 li s9, 18
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
 bne a0, t1, cred_bad
 bnez t0, cred_bad
 bnez a1, cred_bad
 bnez a2, cred_bad
 bnez a3, cred_bad
 bnez a4, cred_bad
 bnez a5, cred_bad
 bnez a6, cred_bad
 bnez a7, cred_bad
 li s9, 19
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
 bnez a0, cred_bad
 bnez t0, cred_bad
 bnez a1, cred_bad
 bnez a2, cred_bad
 bnez a4, cred_bad
 bnez a5, cred_bad
 bnez a6, cred_bad
 bnez a7, cred_bad
 mv s7, a3
 li s9, 20
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
 bne a0, t1, cred_bad
 bnez t0, cred_bad
 bnez a1, cred_bad
 bnez a2, cred_bad
 bnez a3, cred_bad
 bnez a4, cred_bad
 bnez a5, cred_bad
 bnez a6, cred_bad
 bnez a7, cred_bad
 li t2, 576460752303423488
 sub s8, s7, t2
 li s9, 21
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
 bnez a0, cred_bad
 bnez t0, cred_bad
 li t1, 3
 bne a1, t1, cred_bad
 bnez a2, cred_bad
 bnez a3, cred_bad
 li t1, 4294967296
 bne a4, t1, cred_bad
 bnez a5, cred_bad
 bnez a6, cred_bad
 bnez a7, cred_bad
 li s9, 22
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
 bne a0, t1, cred_bad
 bnez t0, cred_bad
 bnez a1, cred_bad
 bnez a2, cred_bad
 bnez a3, cred_bad
 bnez a4, cred_bad
 bnez a5, cred_bad
 bnez a6, cred_bad
 bnez a7, cred_bad
 fmv.x.d t0, f31
 li t1, 4660
 bne t0, t1, cred_bad
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
 li s9, 23
 li a0, 0
 li a1, 0
 li a2, 0
 li a3, 0
 li a4, 0
 li a5, 0
 li a6, 0
 li a7, SYS_GETCRED
 ecall
 bnez a0, cred_bad
 bnez t0, cred_bad
 li t1, 50
 bne a1, t1, cred_bad
 li t1, 60
 bne a2, t1, cred_bad
 li t1, 70
 bne a3, t1, cred_bad
 li t1, 80
 bne a4, t1, cred_bad
 bnez a5, cred_bad
 bnez a6, cred_bad
 bnez a7, cred_bad
 li s9, 24
 li a0, 0
 li a1, 0
 li a2, 0
 li a3, 0
 li a4, 0
 li a5, 0
 li a6, 0
 li a7, SYS_RECV
 ecall
 bnez a0, cred_bad
 li t1, 1
 bne t0, t1, cred_bad
 li t1, 1
 bne a1, t1, cred_bad
 mv t1, s4
 bne a2, t1, cred_bad
 li t1, 10
 bne a4, t1, cred_bad
 mv t1, s3
 bne a5, t1, cred_bad
 li t1, 1
 bne a6, t1, cred_bad
 bnez a7, cred_bad
 mv s7, a3
 li s9, 25
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
 bnez a0, cred_bad
 bnez t0, cred_bad
 li t1, 10
 bne a1, t1, cred_bad
 li t1, 20
 bne a2, t1, cred_bad
 li t1, 30
 bne a3, t1, cred_bad
 li t1, 40
 bne a4, t1, cred_bad
 bnez a5, cred_bad
 bnez a6, cred_bad
 bnez a7, cred_bad
 li s9, 26
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
 bnez a0, cred_bad
 bnez t0, cred_bad
 li t1, 110
 bne a1, t1, cred_bad
 li t1, 120
 bne a2, t1, cred_bad
 li t1, 130
 bne a3, t1, cred_bad
 li t1, 140
 bne a4, t1, cred_bad
 bnez a5, cred_bad
 bnez a6, cred_bad
 bnez a7, cred_bad
 li s9, 27
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
 bnez a0, cred_bad
 bnez t0, cred_bad
 li t1, 110
 bne a1, t1, cred_bad
 li t1, 120
 bne a2, t1, cred_bad
 li t1, 130
 bne a3, t1, cred_bad
 li t1, 140
 bne a4, t1, cred_bad
 bnez a5, cred_bad
 bnez a6, cred_bad
 bnez a7, cred_bad
 li s9, 28
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
 bne a0, t1, cred_bad
 bnez t0, cred_bad
 bnez a1, cred_bad
 bnez a2, cred_bad
 bnez a3, cred_bad
 bnez a4, cred_bad
 bnez a5, cred_bad
 bnez a6, cred_bad
 bnez a7, cred_bad
 li s9, 29
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
 bne a0, t1, cred_bad
 bnez t0, cred_bad
 bnez a1, cred_bad
 bnez a2, cred_bad
 bnez a3, cred_bad
 bnez a4, cred_bad
 bnez a5, cred_bad
 bnez a6, cred_bad
 bnez a7, cred_bad
 li s9, 30
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
 bne a0, t1, cred_bad
 bnez t0, cred_bad
 bnez a1, cred_bad
 bnez a2, cred_bad
 bnez a3, cred_bad
 bnez a4, cred_bad
 bnez a5, cred_bad
 bnez a6, cred_bad
 bnez a7, cred_bad
 li s9, 31
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
 bne a0, t1, cred_bad
 bnez t0, cred_bad
 bnez a1, cred_bad
 bnez a2, cred_bad
 bnez a3, cred_bad
 bnez a4, cred_bad
 bnez a5, cred_bad
 bnez a6, cred_bad
 bnez a7, cred_bad
 li s9, 32
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
 bnez a0, cred_bad
 bnez t0, cred_bad
 bnez a1, cred_bad
 bnez a2, cred_bad
 bnez a3, cred_bad
 bnez a4, cred_bad
 bnez a5, cred_bad
 bnez a6, cred_bad
 bnez a7, cred_bad
 li s9, 33
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
 bnez a0, cred_bad
 bnez t0, cred_bad
 bnez a1, cred_bad
 bnez a2, cred_bad
 bnez a3, cred_bad
 bnez a4, cred_bad
 bnez a5, cred_bad
 bnez a6, cred_bad
 bnez a7, cred_bad
 li s9, 34
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
 bnez a0, cred_bad
 bnez t0, cred_bad
 li t1, 3
 bne a1, t1, cred_bad
 bnez a2, cred_bad
 bnez a3, cred_bad
 li t1, 8589934592
 bne a4, t1, cred_bad
 bnez a5, cred_bad
 bnez a6, cred_bad
 bnez a7, cred_bad
 li s9, 35
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
 bnez a0, cred_bad
 bnez t0, cred_bad
 li t1, 10
 bne a1, t1, cred_bad
 li t1, 20
 bne a2, t1, cred_bad
 li t1, 30
 bne a3, t1, cred_bad
 li t1, 40
 bne a4, t1, cred_bad
 bnez a5, cred_bad
 bnez a6, cred_bad
 bnez a7, cred_bad
 li s9, 36
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
 bnez a0, cred_bad
 bnez t0, cred_bad
 li t1, 110
 bne a1, t1, cred_bad
 li t1, 120
 bne a2, t1, cred_bad
 li t1, 130
 bne a3, t1, cred_bad
 li t1, 140
 bne a4, t1, cred_bad
 bnez a5, cred_bad
 bnez a6, cred_bad
 bnez a7, cred_bad
 li s9, 37
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
 bnez a0, cred_bad
 bnez t0, cred_bad
 li t1, 10
 bne a1, t1, cred_bad
 li t1, 20
 bne a2, t1, cred_bad
 li t1, 30
 bne a3, t1, cred_bad
 li t1, 40
 bne a4, t1, cred_bad
 bnez a5, cred_bad
 bnez a6, cred_bad
 bnez a7, cred_bad
 li s9, 38
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
 bnez a0, cred_bad
 bnez t0, cred_bad
 li t1, 10
 bne a1, t1, cred_bad
 li t1, 20
 bne a2, t1, cred_bad
 li t1, 30
 bne a3, t1, cred_bad
 li t1, 40
 bne a4, t1, cred_bad
 bnez a5, cred_bad
 bnez a6, cred_bad
 bnez a7, cred_bad
 li t0, 262208
 li t1, 1
 sd t1, 0(t0)
 li s9, 39
 li a0, 0
 li a1, 0
 li a2, 0
 li a3, 0
 li a4, 0
 li a5, 0
 li a6, 0
 li a7, SYS_YIELD
 ecall
 bnez a0, cred_bad
 bnez t0, cred_bad
 bnez a1, cred_bad
 bnez a2, cred_bad
 bnez a3, cred_bad
 bnez a4, cred_bad
 bnez a5, cred_bad
 bnez a6, cred_bad
 bnez a7, cred_bad
 li s9, 40
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
 bne a0, t1, cred_bad
 bnez t0, cred_bad
 bnez a1, cred_bad
 bnez a2, cred_bad
 bnez a3, cred_bad
 bnez a4, cred_bad
 bnez a5, cred_bad
 bnez a6, cred_bad
 bnez a7, cred_bad
 li s9, 41
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
 bnez a0, cred_bad
 bnez t0, cred_bad
 li t1, 10
 bne a1, t1, cred_bad
 li t1, 20
 bne a2, t1, cred_bad
 li t1, 30
 bne a3, t1, cred_bad
 li t1, 40
 bne a4, t1, cred_bad
 bnez a5, cred_bad
 bnez a6, cred_bad
 bnez a7, cred_bad
 li s9, 42
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
 bnez a0, cred_bad
 bnez t0, cred_bad
 bnez a1, cred_bad
 bnez a2, cred_bad
 bnez a3, cred_bad
 bnez a4, cred_bad
 bnez a5, cred_bad
 bnez a6, cred_bad
 bnez a7, cred_bad
 li s9, 43
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
 bne a0, t1, cred_bad
 bnez t0, cred_bad
 bnez a1, cred_bad
 bnez a2, cred_bad
 bnez a3, cred_bad
 bnez a4, cred_bad
 bnez a5, cred_bad
 bnez a6, cred_bad
 bnez a7, cred_bad
 li s9, 44
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
 bnez a0, cred_bad
 bnez t0, cred_bad
 bnez a1, cred_bad
 bnez a2, cred_bad
 bnez a3, cred_bad
 bnez a4, cred_bad
 bnez a5, cred_bad
 bnez a6, cred_bad
 bnez a7, cred_bad
 li s9, 45
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
 bnez a0, cred_bad
 bnez t0, cred_bad
 bnez a1, cred_bad
 bnez a2, cred_bad
 bnez a3, cred_bad
 bnez a4, cred_bad
 bnez a5, cred_bad
 bnez a6, cred_bad
 bnez a7, cred_bad
 li s9, 46
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
 bnez a0, cred_bad
 bnez t0, cred_bad
 li t1, 10
 bne a1, t1, cred_bad
 li t1, 20
 bne a2, t1, cred_bad
 li t1, 30
 bne a3, t1, cred_bad
 li t1, 40
 bne a4, t1, cred_bad
 bnez a5, cred_bad
 bnez a6, cred_bad
 bnez a7, cred_bad
 li s9, 47
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
 bne a0, t1, cred_bad
 bnez t0, cred_bad
 bnez a1, cred_bad
 bnez a2, cred_bad
 bnez a3, cred_bad
 bnez a4, cred_bad
 bnez a5, cred_bad
 bnez a6, cred_bad
 bnez a7, cred_bad
 fmv.x.d t0, f31
 li t1, 4660
 bne t0, t1, cred_bad
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
 li s9, 48
 li a0, 0
 li a1, 0
 li a2, 0
 li a3, 0
 li a4, 0
 li a5, 0
 li a6, 0
 li a7, SYS_GETCRED
 ecall
 bnez a0, cred_bad
 bnez t0, cred_bad
 li t1, 210
 bne a1, t1, cred_bad
 li t1, 220
 bne a2, t1, cred_bad
 li t1, 230
 bne a3, t1, cred_bad
 li t1, 240
 bne a4, t1, cred_bad
 bnez a5, cred_bad
 bnez a6, cred_bad
 bnez a7, cred_bad
 li s9, 49
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
 bnez a0, cred_bad
 bnez t0, cred_bad
 bnez a1, cred_bad
 bnez a2, cred_bad
 bnez a3, cred_bad
 bnez a4, cred_bad
 bnez a5, cred_bad
 bnez a6, cred_bad
 bnez a7, cred_bad
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
