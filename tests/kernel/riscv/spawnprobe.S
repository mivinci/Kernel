#include <abi/syscall.h>
#include <abi/ipc.h>
.option norvc
.section .rodata
.balign 8
.global spawn_begin,spawn_end,spawn_victim,spawn_killer,spawn_child_begin,spawn_child_end
spawn_begin:
 li t0, 4660
 fmv.d.x f31, t0
 li t0, 262144
 ld t2, 0(t0)
 li s9, 1
 li a0, 0
 li a1, 0
 li a2, 0
 li a3, 0
 li a4, 0
 li a5, 0
 li a6, 0
 mv a0, t2
 li a7, SYS_FREE
 ecall
 li t1, -3
 beq a0, t1, 99f
 j spawn_bad
99:
 beqz t0, 99f
 j spawn_bad
99:
 beqz a1, 99f
 j spawn_bad
99:
 beqz a2, 99f
 j spawn_bad
99:
 beqz a3, 99f
 j spawn_bad
99:
 beqz a4, 99f
 j spawn_bad
99:
 beqz a5, 99f
 j spawn_bad
99:
 beqz a6, 99f
 j spawn_bad
99:
 beqz a7, 99f
 j spawn_bad
99:
 li t0, 262400
 li t1, 64
 sd t1, 0(t0)
 li t1, 128
 sd t1, 8(t0)
 li t1, 4294967304
 sd t1, 16(t0)
 li t1, 85899345930
 sd t1, 24(t0)
 li t1, 171798691870
 sd t1, 32(t0)
 li t1, 0
 sd t1, 40(t0)
 li t0, 262144
 ld t2, 40(t0)
 li s9, 2
 li a0, 0
 li a1, 0
 li a2, 0
 li a3, 0
 li a4, 0
 li a5, 0
 li a6, 0
 mv a0, t2
 li a1, 262400
 li a7, SYS_CREATE
 ecall
 li t1, -3
 beq a0, t1, 99f
 j spawn_bad
99:
 beqz t0, 99f
 j spawn_bad
99:
 beqz a1, 99f
 j spawn_bad
99:
 beqz a2, 99f
 j spawn_bad
99:
 beqz a3, 99f
 j spawn_bad
99:
 beqz a4, 99f
 j spawn_bad
99:
 beqz a5, 99f
 j spawn_bad
99:
 beqz a6, 99f
 j spawn_bad
99:
 beqz a7, 99f
 j spawn_bad
99:
 li t0, 262144
 ld t2, 0(t0)
 li s9, 3
 li a0, 0
 li a1, 0
 li a2, 0
 li a3, 0
 li a4, 0
 li a5, 0
 li a6, 0
 mv a0, t2
 li a7, SYS_CREATE
 ecall
 li t1, -2
 beq a0, t1, 99f
 j spawn_bad
99:
 beqz t0, 99f
 j spawn_bad
99:
 beqz a1, 99f
 j spawn_bad
99:
 beqz a2, 99f
 j spawn_bad
99:
 beqz a3, 99f
 j spawn_bad
99:
 beqz a4, 99f
 j spawn_bad
99:
 beqz a5, 99f
 j spawn_bad
99:
 beqz a6, 99f
 j spawn_bad
99:
 beqz a7, 99f
 j spawn_bad
99:
 li t0, 262144
 ld t2, 0(t0)
 li s9, 4
 li a0, 0
 li a1, 0
 li a2, 0
 li a3, 0
 li a4, 0
 li a5, 0
 li a6, 0
 mv a0, t2
 li a1, 266232
 li a7, SYS_CREATE
 ecall
 li t1, -2
 beq a0, t1, 99f
 j spawn_bad
99:
 beqz t0, 99f
 j spawn_bad
99:
 beqz a1, 99f
 j spawn_bad
99:
 beqz a2, 99f
 j spawn_bad
99:
 beqz a3, 99f
 j spawn_bad
99:
 beqz a4, 99f
 j spawn_bad
99:
 beqz a5, 99f
 j spawn_bad
99:
 beqz a6, 99f
 j spawn_bad
99:
 beqz a7, 99f
 j spawn_bad
99:
 li t0, 262144
 ld t2, 0(t0)
 li s9, 5
 li a0, 0
 li a1, 0
 li a2, 0
 li a3, 0
 li a4, 0
 li a5, 0
 li a6, 0
 mv a0, t2
 li a1, 262400
 li a2, 1
 li a7, SYS_CREATE
 ecall
 li t1, -1
 beq a0, t1, 99f
 j spawn_bad
99:
 beqz t0, 99f
 j spawn_bad
99:
 beqz a1, 99f
 j spawn_bad
99:
 beqz a2, 99f
 j spawn_bad
99:
 beqz a3, 99f
 j spawn_bad
99:
 beqz a4, 99f
 j spawn_bad
99:
 beqz a5, 99f
 j spawn_bad
99:
 beqz a6, 99f
 j spawn_bad
99:
 beqz a7, 99f
 j spawn_bad
99:
 li t0, 262400
 li t1, 64
 sd t1, 0(t0)
 li t1, 128
 sd t1, 8(t0)
 li t1, 4294967304
 sd t1, 16(t0)
 li t1, 85899345930
 sd t1, 24(t0)
 li t1, 171798691870
 sd t1, 32(t0)
 li t1, 0
 sd t1, 40(t0)
 li t0, 262400
 li t1, 0
 sd t1, 0(t0)
 li t0, 262144
 ld t2, 0(t0)
 li s9, 6
 li a0, 0
 li a1, 0
 li a2, 0
 li a3, 0
 li a4, 0
 li a5, 0
 li a6, 0
 mv a0, t2
 li a1, 262400
 li a7, SYS_CREATE
 ecall
 li t1, -1
 beq a0, t1, 99f
 j spawn_bad
99:
 beqz t0, 99f
 j spawn_bad
99:
 beqz a1, 99f
 j spawn_bad
99:
 beqz a2, 99f
 j spawn_bad
99:
 beqz a3, 99f
 j spawn_bad
99:
 beqz a4, 99f
 j spawn_bad
99:
 beqz a5, 99f
 j spawn_bad
99:
 beqz a6, 99f
 j spawn_bad
99:
 beqz a7, 99f
 j spawn_bad
99:
 li t0, 262400
 li t1, 64
 sd t1, 0(t0)
 li t1, 128
 sd t1, 8(t0)
 li t1, 4294967304
 sd t1, 16(t0)
 li t1, 85899345930
 sd t1, 24(t0)
 li t1, 171798691870
 sd t1, 32(t0)
 li t1, 0
 sd t1, 40(t0)
 li t0, 262408
 li t1, 1
 sd t1, 0(t0)
 li t0, 262144
 ld t2, 0(t0)
 li s9, 7
 li a0, 0
 li a1, 0
 li a2, 0
 li a3, 0
 li a4, 0
 li a5, 0
 li a6, 0
 mv a0, t2
 li a1, 262400
 li a7, SYS_CREATE
 ecall
 li t1, -1
 beq a0, t1, 99f
 j spawn_bad
99:
 beqz t0, 99f
 j spawn_bad
99:
 beqz a1, 99f
 j spawn_bad
99:
 beqz a2, 99f
 j spawn_bad
99:
 beqz a3, 99f
 j spawn_bad
99:
 beqz a4, 99f
 j spawn_bad
99:
 beqz a5, 99f
 j spawn_bad
99:
 beqz a6, 99f
 j spawn_bad
99:
 beqz a7, 99f
 j spawn_bad
99:
 li t0, 262400
 li t1, 64
 sd t1, 0(t0)
 li t1, 128
 sd t1, 8(t0)
 li t1, 4294967304
 sd t1, 16(t0)
 li t1, 85899345930
 sd t1, 24(t0)
 li t1, 171798691870
 sd t1, 32(t0)
 li t1, 0
 sd t1, 40(t0)
 li t0, 262416
 li t1, 4294967298
 sd t1, 0(t0)
 li t0, 262144
 ld t2, 0(t0)
 li s9, 8
 li a0, 0
 li a1, 0
 li a2, 0
 li a3, 0
 li a4, 0
 li a5, 0
 li a6, 0
 mv a0, t2
 li a1, 262400
 li a7, SYS_CREATE
 ecall
 li t1, -1
 beq a0, t1, 99f
 j spawn_bad
99:
 beqz t0, 99f
 j spawn_bad
99:
 beqz a1, 99f
 j spawn_bad
99:
 beqz a2, 99f
 j spawn_bad
99:
 beqz a3, 99f
 j spawn_bad
99:
 beqz a4, 99f
 j spawn_bad
99:
 beqz a5, 99f
 j spawn_bad
99:
 beqz a6, 99f
 j spawn_bad
99:
 beqz a7, 99f
 j spawn_bad
99:
 li t0, 262400
 li t1, 64
 sd t1, 0(t0)
 li t1, 128
 sd t1, 8(t0)
 li t1, 4294967304
 sd t1, 16(t0)
 li t1, 85899345930
 sd t1, 24(t0)
 li t1, 171798691870
 sd t1, 32(t0)
 li t1, 0
 sd t1, 40(t0)
 li t0, 262416
 li t1, 8
 sd t1, 0(t0)
 li t0, 262144
 ld t2, 0(t0)
 li s9, 9
 li a0, 0
 li a1, 0
 li a2, 0
 li a3, 0
 li a4, 0
 li a5, 0
 li a6, 0
 mv a0, t2
 li a1, 262400
 li a7, SYS_CREATE
 ecall
 li t1, -1
 beq a0, t1, 99f
 j spawn_bad
99:
 beqz t0, 99f
 j spawn_bad
99:
 beqz a1, 99f
 j spawn_bad
99:
 beqz a2, 99f
 j spawn_bad
99:
 beqz a3, 99f
 j spawn_bad
99:
 beqz a4, 99f
 j spawn_bad
99:
 beqz a5, 99f
 j spawn_bad
99:
 beqz a6, 99f
 j spawn_bad
99:
 beqz a7, 99f
 j spawn_bad
99:
 li t0, 262400
 li t1, 64
 sd t1, 0(t0)
 li t1, 128
 sd t1, 8(t0)
 li t1, 4294967304
 sd t1, 16(t0)
 li t1, 85899345930
 sd t1, 24(t0)
 li t1, 171798691870
 sd t1, 32(t0)
 li t1, 0
 sd t1, 40(t0)
 li t0, 262440
 li t1, 1
 sd t1, 0(t0)
 li t0, 262144
 ld t2, 0(t0)
 li s9, 10
 li a0, 0
 li a1, 0
 li a2, 0
 li a3, 0
 li a4, 0
 li a5, 0
 li a6, 0
 mv a0, t2
 li a1, 262400
 li a7, SYS_CREATE
 ecall
 li t1, -1
 beq a0, t1, 99f
 j spawn_bad
99:
 beqz t0, 99f
 j spawn_bad
99:
 beqz a1, 99f
 j spawn_bad
99:
 beqz a2, 99f
 j spawn_bad
99:
 beqz a3, 99f
 j spawn_bad
99:
 beqz a4, 99f
 j spawn_bad
99:
 beqz a5, 99f
 j spawn_bad
99:
 beqz a6, 99f
 j spawn_bad
99:
 beqz a7, 99f
 j spawn_bad
99:
 li t0, 262400
 li t1, 64
 sd t1, 0(t0)
 li t1, 256
 sd t1, 8(t0)
 li t1, 4294967304
 sd t1, 16(t0)
 li t1, 85899345930
 sd t1, 24(t0)
 li t1, 171798691870
 sd t1, 32(t0)
 li t1, 0
 sd t1, 40(t0)
 li t0, 262400
 li t1, 129
 sd t1, 0(t0)
 li t0, 262144
 ld t2, 0(t0)
 li s9, 11
 li a0, 0
 li a1, 0
 li a2, 0
 li a3, 0
 li a4, 0
 li a5, 0
 li a6, 0
 mv a0, t2
 li a1, 262400
 li a7, SYS_CREATE
 ecall
 li t1, -5
 beq a0, t1, 99f
 j spawn_bad
99:
 beqz t0, 99f
 j spawn_bad
99:
 beqz a1, 99f
 j spawn_bad
99:
 beqz a2, 99f
 j spawn_bad
99:
 beqz a3, 99f
 j spawn_bad
99:
 beqz a4, 99f
 j spawn_bad
99:
 beqz a5, 99f
 j spawn_bad
99:
 beqz a6, 99f
 j spawn_bad
99:
 beqz a7, 99f
 j spawn_bad
99:
 li t0, 262400
 li t1, 64
 sd t1, 0(t0)
 li t1, 128
 sd t1, 8(t0)
 li t1, 4294967304
 sd t1, 16(t0)
 li t1, 85899345930
 sd t1, 24(t0)
 li t1, 171798691870
 sd t1, 32(t0)
 li t1, 0
 sd t1, 40(t0)
 li t0, 262408
 li t1, 257
 sd t1, 0(t0)
 li t0, 262144
 ld t2, 0(t0)
 li s9, 12
 li a0, 0
 li a1, 0
 li a2, 0
 li a3, 0
 li a4, 0
 li a5, 0
 li a6, 0
 mv a0, t2
 li a1, 262400
 li a7, SYS_CREATE
 ecall
 li t1, -5
 beq a0, t1, 99f
 j spawn_bad
99:
 beqz t0, 99f
 j spawn_bad
99:
 beqz a1, 99f
 j spawn_bad
99:
 beqz a2, 99f
 j spawn_bad
99:
 beqz a3, 99f
 j spawn_bad
99:
 beqz a4, 99f
 j spawn_bad
99:
 beqz a5, 99f
 j spawn_bad
99:
 beqz a6, 99f
 j spawn_bad
99:
 beqz a7, 99f
 j spawn_bad
99:
 li t0, 262400
 li t1, 64
 sd t1, 0(t0)
 li t1, 128
 sd t1, 8(t0)
 li t1, 4294967304
 sd t1, 16(t0)
 li t1, 85899345930
 sd t1, 24(t0)
 li t1, 171798691870
 sd t1, 32(t0)
 li t1, 0
 sd t1, 40(t0)
 li t0, 262416
 li t1, 4294967313
 sd t1, 0(t0)
 li t0, 262144
 ld t2, 0(t0)
 li s9, 13
 li a0, 0
 li a1, 0
 li a2, 0
 li a3, 0
 li a4, 0
 li a5, 0
 li a6, 0
 mv a0, t2
 li a1, 262400
 li a7, SYS_CREATE
 ecall
 li t1, -5
 beq a0, t1, 99f
 j spawn_bad
99:
 beqz t0, 99f
 j spawn_bad
99:
 beqz a1, 99f
 j spawn_bad
99:
 beqz a2, 99f
 j spawn_bad
99:
 beqz a3, 99f
 j spawn_bad
99:
 beqz a4, 99f
 j spawn_bad
99:
 beqz a5, 99f
 j spawn_bad
99:
 beqz a6, 99f
 j spawn_bad
99:
 beqz a7, 99f
 j spawn_bad
99:
 li t0, 262400
 li t1, 64
 sd t1, 0(t0)
 li t1, 128
 sd t1, 8(t0)
 li t1, 4294967304
 sd t1, 16(t0)
 li t1, 85899345930
 sd t1, 24(t0)
 li t1, 171798691870
 sd t1, 32(t0)
 li t1, 0
 sd t1, 40(t0)
 li t0, 262416
 li t1, 12884901896
 sd t1, 0(t0)
 li t0, 262144
 ld t2, 0(t0)
 li s9, 14
 li a0, 0
 li a1, 0
 li a2, 0
 li a3, 0
 li a4, 0
 li a5, 0
 li a6, 0
 mv a0, t2
 li a1, 262400
 li a7, SYS_CREATE
 ecall
 li t1, -5
 beq a0, t1, 99f
 j spawn_bad
99:
 beqz t0, 99f
 j spawn_bad
99:
 beqz a1, 99f
 j spawn_bad
99:
 beqz a2, 99f
 j spawn_bad
99:
 beqz a3, 99f
 j spawn_bad
99:
 beqz a4, 99f
 j spawn_bad
99:
 beqz a5, 99f
 j spawn_bad
99:
 beqz a6, 99f
 j spawn_bad
99:
 beqz a7, 99f
 j spawn_bad
99:
 li t0, 262400
 li t1, 64
 sd t1, 0(t0)
 li t1, 128
 sd t1, 8(t0)
 li t1, 4294967304
 sd t1, 16(t0)
 li t1, 85899345930
 sd t1, 24(t0)
 li t1, 171798691870
 sd t1, 32(t0)
 li t1, 0
 sd t1, 40(t0)
 li t0, 262424
 li t1, 0
 sd t1, 0(t0)
 li t0, 262144
 ld t2, 0(t0)
 li s9, 15
 li a0, 0
 li a1, 0
 li a2, 0
 li a3, 0
 li a4, 0
 li a5, 0
 li a6, 0
 mv a0, t2
 li a1, 262400
 li a7, SYS_CREATE
 ecall
 li t1, -5
 beq a0, t1, 99f
 j spawn_bad
99:
 beqz t0, 99f
 j spawn_bad
99:
 beqz a1, 99f
 j spawn_bad
99:
 beqz a2, 99f
 j spawn_bad
99:
 beqz a3, 99f
 j spawn_bad
99:
 beqz a4, 99f
 j spawn_bad
99:
 beqz a5, 99f
 j spawn_bad
99:
 beqz a6, 99f
 j spawn_bad
99:
 beqz a7, 99f
 j spawn_bad
99:
 li t0, 262400
 li t1, 64
 sd t1, 0(t0)
 li t1, 128
 sd t1, 8(t0)
 li t1, 4294967304
 sd t1, 16(t0)
 li t1, 85899345930
 sd t1, 24(t0)
 li t1, 171798691870
 sd t1, 32(t0)
 li t1, 0
 sd t1, 40(t0)
 li t0, 262144
 ld t2, 24(t0)
 li s9, 16
 li a0, 0
 li a1, 0
 li a2, 0
 li a3, 0
 li a4, 0
 li a5, 0
 li a6, 0
 mv a0, t2
 li a1, 262400
 li a7, SYS_CREATE
 ecall
 li t1, -5
 beq a0, t1, 99f
 j spawn_bad
99:
 beqz t0, 99f
 j spawn_bad
99:
 beqz a1, 99f
 j spawn_bad
99:
 beqz a2, 99f
 j spawn_bad
99:
 beqz a3, 99f
 j spawn_bad
99:
 beqz a4, 99f
 j spawn_bad
99:
 beqz a5, 99f
 j spawn_bad
99:
 beqz a6, 99f
 j spawn_bad
99:
 beqz a7, 99f
 j spawn_bad
99:
 li t0, 262400
 li t1, 1
 sd t1, 0(t0)
 li t1, 2
 sd t1, 8(t0)
 li t1, 4294967304
 sd t1, 16(t0)
 li t1, 85899345930
 sd t1, 24(t0)
 li t1, 171798691870
 sd t1, 32(t0)
 li t1, 0
 sd t1, 40(t0)
 li t0, 262144
 ld t2, 0(t0)
 li s9, 17
 li a0, 0
 li a1, 0
 li a2, 0
 li a3, 0
 li a4, 0
 li a5, 0
 li a6, 0
 mv a0, t2
 li a1, 262400
 li a7, SYS_CREATE
 ecall
 li t1, -8
 beq a0, t1, 99f
 j spawn_bad
99:
 beqz t0, 99f
 j spawn_bad
99:
 beqz a1, 99f
 j spawn_bad
99:
 beqz a2, 99f
 j spawn_bad
99:
 beqz a3, 99f
 j spawn_bad
99:
 beqz a4, 99f
 j spawn_bad
99:
 beqz a5, 99f
 j spawn_bad
99:
 beqz a6, 99f
 j spawn_bad
99:
 beqz a7, 99f
 j spawn_bad
99:
 li t0, 262400
 li t1, 64
 sd t1, 0(t0)
 li t1, 128
 sd t1, 8(t0)
 li t1, 4294967304
 sd t1, 16(t0)
 li t1, 85899345930
 sd t1, 24(t0)
 li t1, 171798691870
 sd t1, 32(t0)
 li t1, 0
 sd t1, 40(t0)
 li t0, 262144
 ld t2, 0(t0)
 li s9, 18
 li a0, 0
 li a1, 0
 li a2, 0
 li a3, 0
 li a4, 0
 li a5, 0
 li a6, 0
 mv a0, t2
 li a1, 262400
 li a7, SYS_CREATE
 ecall
 beqz a0, 99f
 j spawn_bad
99:
 beqz t0, 99f
 j spawn_bad
99:
 beqz a3, 99f
 j spawn_bad
99:
 beqz a4, 99f
 j spawn_bad
99:
 beqz a5, 99f
 j spawn_bad
99:
 beqz a6, 99f
 j spawn_bad
99:
 beqz a7, 99f
 j spawn_bad
99:
 mv s0, a1
 mv s1, a2
 mv s2, a3
 li t0, 262656
 sd s0, 0(t0)
 sd s1, 8(t0)
 li t0, 262400
 li t1, 64
 sd t1, 0(t0)
 li t1, 128
 sd t1, 8(t0)
 li t1, 4294967304
 sd t1, 16(t0)
 li t1, 85899345930
 sd t1, 24(t0)
 li t1, 171798691870
 sd t1, 32(t0)
 li t1, 0
 sd t1, 40(t0)
 li t0, 262144
 ld t2, 0(t0)
 li s9, 19
 li a0, 0
 li a1, 0
 li a2, 0
 li a3, 0
 li a4, 0
 li a5, 0
 li a6, 0
 mv a0, t2
 li a1, 262400
 li a7, SYS_CREATE
 ecall
 beqz a0, 99f
 j spawn_bad
99:
 beqz t0, 99f
 j spawn_bad
99:
 beqz a3, 99f
 j spawn_bad
99:
 beqz a4, 99f
 j spawn_bad
99:
 beqz a5, 99f
 j spawn_bad
99:
 beqz a6, 99f
 j spawn_bad
99:
 beqz a7, 99f
 j spawn_bad
99:
 mv s0, a1
 mv s1, a2
 mv s2, a3
 li t0, 262672
 sd s0, 0(t0)
 sd s1, 8(t0)
 li t0, 262400
 li t1, 64
 sd t1, 0(t0)
 li t1, 128
 sd t1, 8(t0)
 li t1, 4294967304
 sd t1, 16(t0)
 li t1, 85899345930
 sd t1, 24(t0)
 li t1, 171798691870
 sd t1, 32(t0)
 li t1, 0
 sd t1, 40(t0)
 li t0, 262144
 ld t2, 0(t0)
 li s9, 20
 li a0, 0
 li a1, 0
 li a2, 0
 li a3, 0
 li a4, 0
 li a5, 0
 li a6, 0
 mv a0, t2
 li a1, 262400
 li a7, SYS_CREATE
 ecall
 beqz a0, 99f
 j spawn_bad
99:
 beqz t0, 99f
 j spawn_bad
99:
 beqz a3, 99f
 j spawn_bad
99:
 beqz a4, 99f
 j spawn_bad
99:
 beqz a5, 99f
 j spawn_bad
99:
 beqz a6, 99f
 j spawn_bad
99:
 beqz a7, 99f
 j spawn_bad
99:
 mv s0, a1
 mv s1, a2
 mv s2, a3
 li t0, 262688
 sd s0, 0(t0)
 sd s1, 8(t0)
 li t0, 262400
 li t1, 64
 sd t1, 0(t0)
 li t1, 128
 sd t1, 8(t0)
 li t1, 4294967304
 sd t1, 16(t0)
 li t1, 85899345930
 sd t1, 24(t0)
 li t1, 171798691870
 sd t1, 32(t0)
 li t1, 0
 sd t1, 40(t0)
 li t0, 262144
 ld t2, 0(t0)
 li s9, 21
 li a0, 0
 li a1, 0
 li a2, 0
 li a3, 0
 li a4, 0
 li a5, 0
 li a6, 0
 mv a0, t2
 li a1, 262400
 li a7, SYS_CREATE
 ecall
 li t1, -9
 beq a0, t1, 99f
 j spawn_bad
99:
 beqz t0, 99f
 j spawn_bad
99:
 beqz a1, 99f
 j spawn_bad
99:
 beqz a2, 99f
 j spawn_bad
99:
 beqz a3, 99f
 j spawn_bad
99:
 beqz a4, 99f
 j spawn_bad
99:
 beqz a5, 99f
 j spawn_bad
99:
 beqz a6, 99f
 j spawn_bad
99:
 beqz a7, 99f
 j spawn_bad
99:
 li t0, 262656
 ld s0, 0(t0)
 li s9, 22
 li a0, 0
 li a1, 0
 li a2, 0
 li a3, 0
 li a4, 0
 li a5, 0
 li a6, 0
 mv a0, s0
 li a7, SYS_DESTROY
 ecall
 beqz a0, 99f
 j spawn_bad
99:
 beqz t0, 99f
 j spawn_bad
99:
 beqz a1, 99f
 j spawn_bad
99:
 beqz a2, 99f
 j spawn_bad
99:
 beqz a3, 99f
 j spawn_bad
99:
 beqz a4, 99f
 j spawn_bad
99:
 beqz a5, 99f
 j spawn_bad
99:
 beqz a6, 99f
 j spawn_bad
99:
 beqz a7, 99f
 j spawn_bad
99:
 li s9, 23
 li a0, 0
 li a1, 0
 li a2, 0
 li a3, 0
 li a4, 0
 li a5, 0
 li a6, 0
 mv a0, s0
 li a7, SYS_START
 ecall
 li t1, -3
 beq a0, t1, 99f
 j spawn_bad
99:
 beqz t0, 99f
 j spawn_bad
99:
 beqz a1, 99f
 j spawn_bad
99:
 beqz a2, 99f
 j spawn_bad
99:
 beqz a3, 99f
 j spawn_bad
99:
 beqz a4, 99f
 j spawn_bad
99:
 beqz a5, 99f
 j spawn_bad
99:
 beqz a6, 99f
 j spawn_bad
99:
 beqz a7, 99f
 j spawn_bad
99:
 li t0, 262672
 ld s0, 0(t0)
 li s9, 24
 li a0, 0
 li a1, 0
 li a2, 0
 li a3, 0
 li a4, 0
 li a5, 0
 li a6, 0
 mv a0, s0
 li a7, SYS_DESTROY
 ecall
 beqz a0, 99f
 j spawn_bad
99:
 beqz t0, 99f
 j spawn_bad
99:
 beqz a1, 99f
 j spawn_bad
99:
 beqz a2, 99f
 j spawn_bad
99:
 beqz a3, 99f
 j spawn_bad
99:
 beqz a4, 99f
 j spawn_bad
99:
 beqz a5, 99f
 j spawn_bad
99:
 beqz a6, 99f
 j spawn_bad
99:
 beqz a7, 99f
 j spawn_bad
99:
 li s9, 25
 li a0, 0
 li a1, 0
 li a2, 0
 li a3, 0
 li a4, 0
 li a5, 0
 li a6, 0
 mv a0, s0
 li a7, SYS_START
 ecall
 li t1, -3
 beq a0, t1, 99f
 j spawn_bad
99:
 beqz t0, 99f
 j spawn_bad
99:
 beqz a1, 99f
 j spawn_bad
99:
 beqz a2, 99f
 j spawn_bad
99:
 beqz a3, 99f
 j spawn_bad
99:
 beqz a4, 99f
 j spawn_bad
99:
 beqz a5, 99f
 j spawn_bad
99:
 beqz a6, 99f
 j spawn_bad
99:
 beqz a7, 99f
 j spawn_bad
99:
 li t0, 262688
 ld s0, 0(t0)
 li s9, 26
 li a0, 0
 li a1, 0
 li a2, 0
 li a3, 0
 li a4, 0
 li a5, 0
 li a6, 0
 mv a0, s0
 li a7, SYS_DESTROY
 ecall
 beqz a0, 99f
 j spawn_bad
99:
 beqz t0, 99f
 j spawn_bad
99:
 beqz a1, 99f
 j spawn_bad
99:
 beqz a2, 99f
 j spawn_bad
99:
 beqz a3, 99f
 j spawn_bad
99:
 beqz a4, 99f
 j spawn_bad
99:
 beqz a5, 99f
 j spawn_bad
99:
 beqz a6, 99f
 j spawn_bad
99:
 beqz a7, 99f
 j spawn_bad
99:
 li s9, 27
 li a0, 0
 li a1, 0
 li a2, 0
 li a3, 0
 li a4, 0
 li a5, 0
 li a6, 0
 mv a0, s0
 li a7, SYS_START
 ecall
 li t1, -3
 beq a0, t1, 99f
 j spawn_bad
99:
 beqz t0, 99f
 j spawn_bad
99:
 beqz a1, 99f
 j spawn_bad
99:
 beqz a2, 99f
 j spawn_bad
99:
 beqz a3, 99f
 j spawn_bad
99:
 beqz a4, 99f
 j spawn_bad
99:
 beqz a5, 99f
 j spawn_bad
99:
 beqz a6, 99f
 j spawn_bad
99:
 beqz a7, 99f
 j spawn_bad
99:
 li t0, 262400
 li t1, 4096
 sd t1, 0(t0)
 li t1, 1
 sd t1, 8(t0)
 li t1, 0
 sd t1, 16(t0)
 li t1, 0
 sd t1, 24(t0)
 li t1, 0
 sd t1, 32(t0)
 li t1, 0
 sd t1, 40(t0)
 li s5, 0
 li s6, 263168
 li s7, 53
spawn_fill:
 li t0, 262144
 ld t2, 40(t0)
 li s9, 28
 li a0, 0
 li a1, 0
 li a2, 0
 li a3, 0
 li a4, 0
 li a5, 0
 li a6, 0
 mv a0, t2
 li a1, 262400
 li a7, SYS_ALLOC
 ecall
 beqz a0, 99f
 j spawn_bad
99:
 beqz t0, 99f
 j spawn_bad
99:
 beqz a2, 99f
 j spawn_bad
99:
 beqz a3, 99f
 j spawn_bad
99:
 beqz a4, 99f
 j spawn_bad
99:
 beqz a5, 99f
 j spawn_bad
99:
 beqz a6, 99f
 j spawn_bad
99:
 beqz a7, 99f
 j spawn_bad
99:
 sd a1, 0(s6)
 addi s5, s5, 1
 addi s6, s6, 8
 bltu s5, s7, spawn_fill
 li t0, 262400
 li t1, 64
 sd t1, 0(t0)
 li t1, 128
 sd t1, 8(t0)
 li t1, 4294967304
 sd t1, 16(t0)
 li t1, 515396075630
 sd t1, 24(t0)
 li t1, 601295421570
 sd t1, 32(t0)
 li t1, 0
 sd t1, 40(t0)
 li t0, 262144
 ld t2, 16(t0)
 li s9, 29
 li a0, 0
 li a1, 0
 li a2, 0
 li a3, 0
 li a4, 0
 li a5, 0
 li a6, 0
 mv a0, t2
 li a1, 262400
 li a7, SYS_CREATE
 ecall
 li t1, -9
 beq a0, t1, 99f
 j spawn_bad
99:
 beqz t0, 99f
 j spawn_bad
99:
 beqz a1, 99f
 j spawn_bad
99:
 beqz a2, 99f
 j spawn_bad
99:
 beqz a3, 99f
 j spawn_bad
99:
 beqz a4, 99f
 j spawn_bad
99:
 beqz a5, 99f
 j spawn_bad
99:
 beqz a6, 99f
 j spawn_bad
99:
 beqz a7, 99f
 j spawn_bad
99:
 li t0, 262400
 li t1, 64
 sd t1, 0(t0)
 li t1, 128
 sd t1, 8(t0)
 li t1, 4294967304
 sd t1, 16(t0)
 li t1, 85899345930
 sd t1, 24(t0)
 li t1, 171798691870
 sd t1, 32(t0)
 li t1, 0
 sd t1, 40(t0)
 li t0, 262144
 ld t2, 0(t0)
 li s9, 30
 li a0, 0
 li a1, 0
 li a2, 0
 li a3, 0
 li a4, 0
 li a5, 0
 li a6, 0
 mv a0, t2
 li a1, 262400
 li a7, SYS_CREATE
 ecall
 beqz a0, 99f
 j spawn_bad
99:
 beqz t0, 99f
 j spawn_bad
99:
 beqz a3, 99f
 j spawn_bad
99:
 beqz a4, 99f
 j spawn_bad
99:
 beqz a5, 99f
 j spawn_bad
99:
 beqz a6, 99f
 j spawn_bad
99:
 beqz a7, 99f
 j spawn_bad
99:
 mv s0, a1
 mv s1, a2
 mv s2, a3
 li s9, 31
 li a0, 0
 li a1, 0
 li a2, 0
 li a3, 0
 li a4, 0
 li a5, 0
 li a6, 0
 mv a0, s0
 li a7, SYS_DESTROY
 ecall
 beqz a0, 99f
 j spawn_bad
99:
 beqz t0, 99f
 j spawn_bad
99:
 beqz a1, 99f
 j spawn_bad
99:
 beqz a2, 99f
 j spawn_bad
99:
 beqz a3, 99f
 j spawn_bad
99:
 beqz a4, 99f
 j spawn_bad
99:
 beqz a5, 99f
 j spawn_bad
99:
 beqz a6, 99f
 j spawn_bad
99:
 beqz a7, 99f
 j spawn_bad
99:
 li t0, 262400
 li t1, 4096
 sd t1, 0(t0)
 li t1, 1
 sd t1, 8(t0)
 li t1, 0
 sd t1, 16(t0)
 li t1, 0
 sd t1, 24(t0)
 li t1, 0
 sd t1, 32(t0)
 li t1, 0
 sd t1, 40(t0)
 li t0, 262144
 ld t2, 40(t0)
 li s9, 32
 li a0, 0
 li a1, 0
 li a2, 0
 li a3, 0
 li a4, 0
 li a5, 0
 li a6, 0
 mv a0, t2
 li a1, 262400
 li a7, SYS_ALLOC
 ecall
 beqz a0, 99f
 j spawn_bad
99:
 beqz t0, 99f
 j spawn_bad
99:
 beqz a2, 99f
 j spawn_bad
99:
 beqz a3, 99f
 j spawn_bad
99:
 beqz a4, 99f
 j spawn_bad
99:
 beqz a5, 99f
 j spawn_bad
99:
 beqz a6, 99f
 j spawn_bad
99:
 beqz a7, 99f
 j spawn_bad
99:
 sd a1, 0(s6)
 li t0, 262400
 li t1, 64
 sd t1, 0(t0)
 li t1, 128
 sd t1, 8(t0)
 li t1, 4294967304
 sd t1, 16(t0)
 li t1, 85899345930
 sd t1, 24(t0)
 li t1, 171798691870
 sd t1, 32(t0)
 li t1, 0
 sd t1, 40(t0)
 li t0, 262144
 ld t2, 0(t0)
 li s9, 33
 li a0, 0
 li a1, 0
 li a2, 0
 li a3, 0
 li a4, 0
 li a5, 0
 li a6, 0
 mv a0, t2
 li a1, 262400
 li a7, SYS_CREATE
 ecall
 li t1, -9
 beq a0, t1, 99f
 j spawn_bad
99:
 beqz t0, 99f
 j spawn_bad
99:
 beqz a1, 99f
 j spawn_bad
99:
 beqz a2, 99f
 j spawn_bad
99:
 beqz a3, 99f
 j spawn_bad
99:
 beqz a4, 99f
 j spawn_bad
99:
 beqz a5, 99f
 j spawn_bad
99:
 beqz a6, 99f
 j spawn_bad
99:
 beqz a7, 99f
 j spawn_bad
99:
 li s5, 0
 li s6, 263168
 li s7, 54
spawn_free:
 ld t2, 0(s6)
 li s9, 34
 li a0, 0
 li a1, 0
 li a2, 0
 li a3, 0
 li a4, 0
 li a5, 0
 li a6, 0
 mv a0, t2
 li a7, SYS_FREE
 ecall
 beqz a0, 99f
 j spawn_bad
99:
 beqz t0, 99f
 j spawn_bad
99:
 beqz a1, 99f
 j spawn_bad
99:
 beqz a2, 99f
 j spawn_bad
99:
 beqz a3, 99f
 j spawn_bad
99:
 beqz a4, 99f
 j spawn_bad
99:
 beqz a5, 99f
 j spawn_bad
99:
 beqz a6, 99f
 j spawn_bad
99:
 beqz a7, 99f
 j spawn_bad
99:
 addi s5, s5, 1
 addi s6, s6, 8
 bltu s5, s7, spawn_free
 li t0, 262272
 li t1, 1
 sd t1, 0(t0)
 li s9, 35
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
 j spawn_bad
99:
 beqz t0, 99f
 j spawn_bad
99:
 beqz a1, 99f
 j spawn_bad
99:
 beqz a2, 99f
 j spawn_bad
99:
 beqz a3, 99f
 j spawn_bad
99:
 beqz a4, 99f
 j spawn_bad
99:
 beqz a5, 99f
 j spawn_bad
99:
 beqz a6, 99f
 j spawn_bad
99:
 beqz a7, 99f
 j spawn_bad
99:
 li t0, 262400
 li t1, 64
 sd t1, 0(t0)
 li t1, 128
 sd t1, 8(t0)
 li t1, 4294967304
 sd t1, 16(t0)
 li t1, 85899345930
 sd t1, 24(t0)
 li t1, 171798691870
 sd t1, 32(t0)
 li t1, 0
 sd t1, 40(t0)
 li t0, 262144
 ld t2, 0(t0)
 li s9, 36
 li a0, 0
 li a1, 0
 li a2, 0
 li a3, 0
 li a4, 0
 li a5, 0
 li a6, 0
 mv a0, t2
 li a1, 262400
 li a7, SYS_CREATE
 ecall
 beqz a0, 99f
 j spawn_bad
99:
 beqz t0, 99f
 j spawn_bad
99:
 beqz a3, 99f
 j spawn_bad
99:
 beqz a4, 99f
 j spawn_bad
99:
 beqz a5, 99f
 j spawn_bad
99:
 beqz a6, 99f
 j spawn_bad
99:
 beqz a7, 99f
 j spawn_bad
99:
 mv s0, a1
 mv s1, a2
 mv s2, a3
 li t0, 262288
 sd s1, 0(t0)
 li s9, 37
 li a0, 0
 li a1, 0
 li a2, 0
 li a3, 0
 li a4, 0
 li a5, 0
 li a6, 0
 mv a0, s0
 li a7, SYS_START
 ecall
 li t1, -6
 beq a0, t1, 99f
 j spawn_bad
99:
 beqz t0, 99f
 j spawn_bad
99:
 beqz a1, 99f
 j spawn_bad
99:
 beqz a2, 99f
 j spawn_bad
99:
 beqz a3, 99f
 j spawn_bad
99:
 beqz a4, 99f
 j spawn_bad
99:
 beqz a5, 99f
 j spawn_bad
99:
 beqz a6, 99f
 j spawn_bad
99:
 beqz a7, 99f
 j spawn_bad
99:
 li s9, 38
 li a0, 0
 li a1, 0
 li a2, 0
 li a3, 0
 li a4, 0
 li a5, 0
 li a6, 0
 mv a0, s0
 li a1, 5
 li a2, 9223372036854775809
 li a7, SYS_CONNECT
 ecall
 li t1, -5
 beq a0, t1, 99f
 j spawn_bad
99:
 beqz t0, 99f
 j spawn_bad
99:
 beqz a1, 99f
 j spawn_bad
99:
 beqz a2, 99f
 j spawn_bad
99:
 beqz a3, 99f
 j spawn_bad
99:
 beqz a4, 99f
 j spawn_bad
99:
 beqz a5, 99f
 j spawn_bad
99:
 beqz a6, 99f
 j spawn_bad
99:
 beqz a7, 99f
 j spawn_bad
99:
 li s9, 39
 li a0, 0
 li a1, 0
 li a2, 0
 li a3, 0
 li a4, 0
 li a5, 0
 li a6, 0
 mv a0, s0
 li a1, 5
 li a2, 1
 li a7, SYS_CONNECT
 ecall
 beqz a0, 99f
 j spawn_bad
99:
 beqz t0, 99f
 j spawn_bad
99:
 beqz a2, 99f
 j spawn_bad
99:
 beqz a3, 99f
 j spawn_bad
99:
 beqz a4, 99f
 j spawn_bad
99:
 beqz a5, 99f
 j spawn_bad
99:
 beqz a6, 99f
 j spawn_bad
99:
 beqz a7, 99f
 j spawn_bad
99:
 mv s4, a1
 li s9, 40
 li a0, 0
 li a1, 0
 li a2, 0
 li a3, 0
 li a4, 0
 li a5, 0
 li a6, 0
 mv a0, s4
 li a5, 1
 li a7, SYS_CALL
 ecall
 li t1, -6
 beq a0, t1, 99f
 j spawn_bad
99:
 beqz t0, 99f
 j spawn_bad
99:
 beqz a1, 99f
 j spawn_bad
99:
 beqz a2, 99f
 j spawn_bad
99:
 beqz a3, 99f
 j spawn_bad
99:
 beqz a4, 99f
 j spawn_bad
99:
 beqz a5, 99f
 j spawn_bad
99:
 beqz a6, 99f
 j spawn_bad
99:
 beqz a7, 99f
 j spawn_bad
99:
 li t0, 262400
 li t1, 4096
 sd t1, 0(t0)
 li t1, 3
 sd t1, 8(t0)
 li t1, 0
 sd t1, 16(t0)
 li t1, 0
 sd t1, 24(t0)
 li t1, 0
 sd t1, 32(t0)
 li t1, 0
 sd t1, 40(t0)
 li s9, 41
 li a0, 0
 li a1, 0
 li a2, 0
 li a3, 0
 li a4, 0
 li a5, 0
 li a6, 0
 mv a0, s0
 li a1, 262400
 li a7, SYS_ALLOC
 ecall
 beqz a0, 99f
 j spawn_bad
99:
 beqz t0, 99f
 j spawn_bad
99:
 beqz a2, 99f
 j spawn_bad
99:
 beqz a3, 99f
 j spawn_bad
99:
 beqz a4, 99f
 j spawn_bad
99:
 beqz a5, 99f
 j spawn_bad
99:
 beqz a6, 99f
 j spawn_bad
99:
 beqz a7, 99f
 j spawn_bad
99:
 mv s3, a1
 li t0, 262144
 ld t2, 32(t0)
 li t0, 262464
 mv t1, s3
 sd t1, 0(t0)
 li t1, 0
 sd t1, 8(t0)
 li t1, 327680
 sd t1, 16(t0)
 li t1, 4096
 sd t1, 24(t0)
 li t1, 3
 sd t1, 32(t0)
 li t1, 0
 sd t1, 40(t0)
 li s9, 42
 li a0, 0
 li a1, 0
 li a2, 0
 li a3, 0
 li a4, 0
 li a5, 0
 li a6, 0
 mv a0, t2
 li a1, 262464
 li a7, SYS_MAP
 ecall
 beqz a0, 99f
 j spawn_bad
99:
 beqz t0, 99f
 j spawn_bad
99:
 beqz a1, 99f
 j spawn_bad
99:
 beqz a2, 99f
 j spawn_bad
99:
 beqz a3, 99f
 j spawn_bad
99:
 beqz a4, 99f
 j spawn_bad
99:
 beqz a5, 99f
 j spawn_bad
99:
 beqz a6, 99f
 j spawn_bad
99:
 beqz a7, 99f
 j spawn_bad
99:
 lla t0, spawn_child_begin
 li t1, 327680
 lla t3, spawn_child_end
spawn_copy_0:
 ld t2, 0(t0)
 sd t2, 0(t1)
 addi t0, t0, 8
 addi t1, t1, 8
 bltu t0, t3, spawn_copy_0
 lla t2, spawn_child_params
 lla t3, spawn_child_begin
 sub t2, t2, t3
 li t0, 327680
 add t0, t0, t2
 sd s1, 0(t0)
 li t0, 262144
 ld t2, 48(t0)
 lla t1, spawn_child_params
 lla t3, spawn_child_begin
 sub t1, t1, t3
 li t0, 327680
 add t0, t0, t1
 sd t2, 8(t0)
 li t1, 10
 sd t1, 16(t0)
 li t1, 20
 sd t1, 24(t0)
 li t1, 30
 sd t1, 32(t0)
 li t1, 40
 sd t1, 40(t0)
 li s9, 43
 li a0, 0
 li a1, 0
 li a2, 0
 li a3, 0
 li a4, 0
 li a5, 0
 li a6, 0
 mv a0, s3
 li a7, SYS_SEAL
 ecall
 li t1, -7
 beq a0, t1, 99f
 j spawn_bad
99:
 beqz t0, 99f
 j spawn_bad
99:
 beqz a1, 99f
 j spawn_bad
99:
 beqz a2, 99f
 j spawn_bad
99:
 beqz a3, 99f
 j spawn_bad
99:
 beqz a4, 99f
 j spawn_bad
99:
 beqz a5, 99f
 j spawn_bad
99:
 beqz a6, 99f
 j spawn_bad
99:
 beqz a7, 99f
 j spawn_bad
99:
 li t0, 262144
 ld t2, 32(t0)
 li s9, 44
 li a0, 0
 li a1, 0
 li a2, 0
 li a3, 0
 li a4, 0
 li a5, 0
 li a6, 0
 mv a0, t2
 li a1, 327680
 li a7, SYS_UNMAP
 ecall
 beqz a0, 99f
 j spawn_bad
99:
 beqz t0, 99f
 j spawn_bad
99:
 beqz a1, 99f
 j spawn_bad
99:
 beqz a2, 99f
 j spawn_bad
99:
 beqz a3, 99f
 j spawn_bad
99:
 beqz a4, 99f
 j spawn_bad
99:
 beqz a5, 99f
 j spawn_bad
99:
 beqz a6, 99f
 j spawn_bad
99:
 beqz a7, 99f
 j spawn_bad
99:
 li s9, 45
 li a0, 0
 li a1, 0
 li a2, 0
 li a3, 0
 li a4, 0
 li a5, 0
 li a6, 0
 mv a0, s3
 li a7, SYS_SEAL
 ecall
 beqz a0, 99f
 j spawn_bad
99:
 beqz t0, 99f
 j spawn_bad
99:
 beqz a1, 99f
 j spawn_bad
99:
 beqz a2, 99f
 j spawn_bad
99:
 beqz a3, 99f
 j spawn_bad
99:
 beqz a4, 99f
 j spawn_bad
99:
 beqz a5, 99f
 j spawn_bad
99:
 beqz a6, 99f
 j spawn_bad
99:
 beqz a7, 99f
 j spawn_bad
99:
 li t0, 262464
 mv t1, s3
 sd t1, 0(t0)
 li t1, 0
 sd t1, 8(t0)
 li t1, 65536
 sd t1, 16(t0)
 li t1, 4096
 sd t1, 24(t0)
 li t1, 5
 sd t1, 32(t0)
 li t1, 0
 sd t1, 40(t0)
 li s9, 46
 li a0, 0
 li a1, 0
 li a2, 0
 li a3, 0
 li a4, 0
 li a5, 0
 li a6, 0
 mv a0, s0
 li a1, 262464
 li a7, SYS_MAP
 ecall
 beqz a0, 99f
 j spawn_bad
99:
 beqz t0, 99f
 j spawn_bad
99:
 beqz a1, 99f
 j spawn_bad
99:
 beqz a2, 99f
 j spawn_bad
99:
 beqz a3, 99f
 j spawn_bad
99:
 beqz a4, 99f
 j spawn_bad
99:
 beqz a5, 99f
 j spawn_bad
99:
 beqz a6, 99f
 j spawn_bad
99:
 beqz a7, 99f
 j spawn_bad
99:
 li s9, 47
 li a0, 0
 li a1, 0
 li a2, 0
 li a3, 0
 li a4, 0
 li a5, 0
 li a6, 0
 mv a0, s0
 mv a1, s3
 li a2, 5
 li a4, 4096
 li a5, 1
 li a7, SYS_GRANT
 ecall
 beqz a0, 99f
 j spawn_bad
99:
 beqz t0, 99f
 j spawn_bad
99:
 beqz a2, 99f
 j spawn_bad
99:
 beqz a3, 99f
 j spawn_bad
99:
 beqz a4, 99f
 j spawn_bad
99:
 beqz a5, 99f
 j spawn_bad
99:
 beqz a6, 99f
 j spawn_bad
99:
 beqz a7, 99f
 j spawn_bad
99:
 li t0, 262400
 li t1, 65536
 sd t1, 0(t0)
 li t1, 1
 sd t1, 8(t0)
 li t1, 0
 sd t1, 16(t0)
 li t1, 0
 sd t1, 24(t0)
 li t1, 0
 sd t1, 32(t0)
 li t1, 0
 sd t1, 40(t0)
 li s9, 48
 li a0, 0
 li a1, 0
 li a2, 0
 li a3, 0
 li a4, 0
 li a5, 0
 li a6, 0
 mv a0, s0
 li a1, 262400
 li a7, SYS_ALLOC
 ecall
 beqz a0, 99f
 j spawn_bad
99:
 beqz t0, 99f
 j spawn_bad
99:
 beqz a2, 99f
 j spawn_bad
99:
 beqz a3, 99f
 j spawn_bad
99:
 beqz a4, 99f
 j spawn_bad
99:
 beqz a5, 99f
 j spawn_bad
99:
 beqz a6, 99f
 j spawn_bad
99:
 beqz a7, 99f
 j spawn_bad
99:
 mv s5, a1
 li t0, 262464
 mv t1, s5
 sd t1, 0(t0)
 li t1, 0
 sd t1, 8(t0)
 li t1, 274877775872
 sd t1, 16(t0)
 li t1, 65536
 sd t1, 24(t0)
 li t1, 3
 sd t1, 32(t0)
 li t1, 0
 sd t1, 40(t0)
 li s9, 49
 li a0, 0
 li a1, 0
 li a2, 0
 li a3, 0
 li a4, 0
 li a5, 0
 li a6, 0
 mv a0, s0
 li a1, 262464
 li a7, SYS_MAP
 ecall
 beqz a0, 99f
 j spawn_bad
99:
 beqz t0, 99f
 j spawn_bad
99:
 beqz a1, 99f
 j spawn_bad
99:
 beqz a2, 99f
 j spawn_bad
99:
 beqz a3, 99f
 j spawn_bad
99:
 beqz a4, 99f
 j spawn_bad
99:
 beqz a5, 99f
 j spawn_bad
99:
 beqz a6, 99f
 j spawn_bad
99:
 beqz a7, 99f
 j spawn_bad
99:
 li t0, 262400
 li t1, 4096
 sd t1, 0(t0)
 li t1, 1
 sd t1, 8(t0)
 li t1, 0
 sd t1, 16(t0)
 li t1, 0
 sd t1, 24(t0)
 li t1, 0
 sd t1, 32(t0)
 li t1, 0
 sd t1, 40(t0)
 li s9, 50
 li a0, 0
 li a1, 0
 li a2, 0
 li a3, 0
 li a4, 0
 li a5, 0
 li a6, 0
 mv a0, s0
 li a1, 262400
 li a7, SYS_ALLOC
 ecall
 beqz a0, 99f
 j spawn_bad
99:
 beqz t0, 99f
 j spawn_bad
99:
 beqz a2, 99f
 j spawn_bad
99:
 beqz a3, 99f
 j spawn_bad
99:
 beqz a4, 99f
 j spawn_bad
99:
 beqz a5, 99f
 j spawn_bad
99:
 beqz a6, 99f
 j spawn_bad
99:
 beqz a7, 99f
 j spawn_bad
99:
 mv s6, a1
 li t0, 262144
 ld t2, 32(t0)
 li t0, 262464
 mv t1, s6
 sd t1, 0(t0)
 li t1, 0
 sd t1, 8(t0)
 li t1, 327680
 sd t1, 16(t0)
 li t1, 4096
 sd t1, 24(t0)
 li t1, 3
 sd t1, 32(t0)
 li t1, 0
 sd t1, 40(t0)
 li s9, 51
 li a0, 0
 li a1, 0
 li a2, 0
 li a3, 0
 li a4, 0
 li a5, 0
 li a6, 0
 mv a0, t2
 li a1, 262464
 li a7, SYS_MAP
 ecall
 beqz a0, 99f
 j spawn_bad
99:
 beqz t0, 99f
 j spawn_bad
99:
 beqz a1, 99f
 j spawn_bad
99:
 beqz a2, 99f
 j spawn_bad
99:
 beqz a3, 99f
 j spawn_bad
99:
 beqz a4, 99f
 j spawn_bad
99:
 beqz a5, 99f
 j spawn_bad
99:
 beqz a6, 99f
 j spawn_bad
99:
 beqz a7, 99f
 j spawn_bad
99:
 li s9, 52
 li a0, 0
 li a1, 0
 li a2, 0
 li a3, 0
 li a4, 0
 li a5, 0
 li a6, 0
 mv a0, s0
 li a1, 5
 li a3, 263168
 li a4, 4
 li a7, SYS_QUERY
 ecall
 beqz a0, 99f
 j spawn_bad
99:
 beqz t0, 99f
 j spawn_bad
99:
 li t1, 4
 beq a1, t1, 99f
 j spawn_bad
99:
 beqz a2, 99f
 j spawn_bad
99:
 beqz a3, 99f
 j spawn_bad
99:
 beqz a4, 99f
 j spawn_bad
99:
 beqz a5, 99f
 j spawn_bad
99:
 beqz a6, 99f
 j spawn_bad
99:
 beqz a7, 99f
 j spawn_bad
99:
 li t0, 263168
 ld t2, 8(t0)
 mv t1, s1
 beq t2, t1, 99f
 j spawn_bad
99:
 li t0, 263168
 ld t2, 16(t0)
 li t1, 1
 beq t2, t1, 99f
 j spawn_bad
99:
 li t0, 263168
 ld t2, 40(t0)
 li t1, 2
 beq t2, t1, 99f
 j spawn_bad
99:
 li t0, 263168
 ld t2, 200(t0)
 mv t1, s1
 beq t2, t1, 99f
 j spawn_bad
99:
 li t0, 263168
 ld t2, 208(t0)
 li t1, 1
 beq t2, t1, 99f
 j spawn_bad
99:
 li t0, 263168
 ld t2, 232(t0)
 li t1, 3
 beq t2, t1, 99f
 j spawn_bad
99:
 li t0, 263168
 ld t2, 392(t0)
 mv t1, s1
 beq t2, t1, 99f
 j spawn_bad
99:
 li t0, 263168
 ld t2, 400(t0)
 li t1, 1
 beq t2, t1, 99f
 j spawn_bad
99:
 li t0, 263168
 ld t2, 424(t0)
 li t1, 1
 beq t2, t1, 99f
 j spawn_bad
99:
 li t0, 263168
 ld t2, 584(t0)
 mv t1, s1
 beq t2, t1, 99f
 j spawn_bad
99:
 li t0, 263168
 ld t2, 592(t0)
 li t1, 1
 beq t2, t1, 99f
 j spawn_bad
99:
 li t0, 263168
 ld t2, 616(t0)
 li t1, 4
 beq t2, t1, 99f
 j spawn_bad
99:
 li t0, 263168
 ld t2, 432(t0)
 li t1, 1
 beq t2, t1, 99f
 j spawn_bad
99:
 li t0, 327680
 li t1, 5422984103440953393
 sd t1, 0(t0)
 li t1, 1
 sd t1, 8(t0)
 li t1, 224
 sd t1, 16(t0)
 li t1, 0
 sd t1, 24(t0)
 li t1, 0
 sd t1, 32(t0)
 li t1, 0
 sd t1, 40(t0)
 li t1, 0
 sd t1, 48(t0)
 li t1, 0
 sd t1, 56(t0)
 li t1, 3
 sd t1, 64(t0)
 li t1, 128
 sd t1, 72(t0)
 li t1, 0
 sd t1, 80(t0)
 li t1, 0
 sd t1, 88(t0)
 li t1, 0
 sd t1, 96(t0)
 li t1, 0
 sd t1, 104(t0)
 li t1, 65536
 sd t1, 112(t0)
 li t1, 274877906944
 sd t1, 120(t0)
 li t0, 263168
 ld t2, 32(t0)
 li t0, 327808
 li t1, 1
 sd t1, 0(t0)
 sd t2, 8(t0)
 li t1, 0
 sd t1, 16(t0)
 sd t1, 24(t0)
 li t0, 263168
 ld t2, 224(t0)
 li t0, 327840
 li t1, 2
 sd t1, 0(t0)
 sd t2, 8(t0)
 li t1, 0
 sd t1, 16(t0)
 sd t1, 24(t0)
 li t0, 263168
 ld t2, 416(t0)
 li t0, 327872
 li t1, 3
 sd t1, 0(t0)
 sd t2, 8(t0)
 li t1, 0
 sd t1, 16(t0)
 sd t1, 24(t0)
 li t0, 262464
 mv t1, s6
 sd t1, 0(t0)
 li t1, 0
 sd t1, 8(t0)
 li t1, 274861129728
 sd t1, 16(t0)
 li t1, 4096
 sd t1, 24(t0)
 li t1, 1
 sd t1, 32(t0)
 li t1, 0
 sd t1, 40(t0)
 li s9, 53
 li a0, 0
 li a1, 0
 li a2, 0
 li a3, 0
 li a4, 0
 li a5, 0
 li a6, 0
 mv a0, s0
 li a1, 262464
 li a7, SYS_MAP
 ecall
 beqz a0, 99f
 j spawn_bad
99:
 beqz t0, 99f
 j spawn_bad
99:
 beqz a1, 99f
 j spawn_bad
99:
 beqz a2, 99f
 j spawn_bad
99:
 beqz a3, 99f
 j spawn_bad
99:
 beqz a4, 99f
 j spawn_bad
99:
 beqz a5, 99f
 j spawn_bad
99:
 beqz a6, 99f
 j spawn_bad
99:
 beqz a7, 99f
 j spawn_bad
99:
 li t0, 262528
 li t1, 65536
 sd t1, 0(t0)
 li t1, 274877841408
 sd t1, 8(t0)
 li t1, 274861129728
 sd t1, 16(t0)
 li t1, 224
 sd t1, 24(t0)
 li t1, 0
 sd t1, 32(t0)
 li t1, 0
 sd t1, 40(t0)
 li s9, 54
 li a0, 0
 li a1, 0
 li a2, 0
 li a3, 0
 li a4, 0
 li a5, 0
 li a6, 0
 mv a0, s0
 li a1, 262528
 li a2, 1
 li a7, SYS_CONFIGURE
 ecall
 li t1, -18
 beq a0, t1, 99f
 j spawn_bad
99:
 beqz t0, 99f
 j spawn_bad
99:
 beqz a1, 99f
 j spawn_bad
99:
 beqz a2, 99f
 j spawn_bad
99:
 beqz a3, 99f
 j spawn_bad
99:
 beqz a4, 99f
 j spawn_bad
99:
 beqz a5, 99f
 j spawn_bad
99:
 beqz a6, 99f
 j spawn_bad
99:
 beqz a7, 99f
 j spawn_bad
99:
 li s9, 55
 li a0, 0
 li a1, 0
 li a2, 0
 li a3, 0
 li a4, 0
 li a5, 0
 li a6, 0
 mv a0, s0
 li a1, 262528
 li a7, SYS_CONFIGURE
 ecall
 beqz a0, 99f
 j spawn_bad
99:
 beqz t0, 99f
 j spawn_bad
99:
 beqz a1, 99f
 j spawn_bad
99:
 beqz a2, 99f
 j spawn_bad
99:
 beqz a3, 99f
 j spawn_bad
99:
 beqz a4, 99f
 j spawn_bad
99:
 beqz a5, 99f
 j spawn_bad
99:
 beqz a6, 99f
 j spawn_bad
99:
 beqz a7, 99f
 j spawn_bad
99:
 li s9, 56
 li a0, 0
 li a1, 0
 li a2, 0
 li a3, 0
 li a4, 0
 li a5, 0
 li a6, 0
 mv a0, s0
 li a7, SYS_START
 ecall
 li t1, -7
 beq a0, t1, 99f
 j spawn_bad
99:
 beqz t0, 99f
 j spawn_bad
99:
 beqz a1, 99f
 j spawn_bad
99:
 beqz a2, 99f
 j spawn_bad
99:
 beqz a3, 99f
 j spawn_bad
99:
 beqz a4, 99f
 j spawn_bad
99:
 beqz a5, 99f
 j spawn_bad
99:
 beqz a6, 99f
 j spawn_bad
99:
 beqz a7, 99f
 j spawn_bad
99:
 li t0, 262144
 ld t2, 32(t0)
 li s9, 57
 li a0, 0
 li a1, 0
 li a2, 0
 li a3, 0
 li a4, 0
 li a5, 0
 li a6, 0
 mv a0, t2
 li a1, 327680
 li a7, SYS_UNMAP
 ecall
 beqz a0, 99f
 j spawn_bad
99:
 beqz t0, 99f
 j spawn_bad
99:
 beqz a1, 99f
 j spawn_bad
99:
 beqz a2, 99f
 j spawn_bad
99:
 beqz a3, 99f
 j spawn_bad
99:
 beqz a4, 99f
 j spawn_bad
99:
 beqz a5, 99f
 j spawn_bad
99:
 beqz a6, 99f
 j spawn_bad
99:
 beqz a7, 99f
 j spawn_bad
99:
 li s9, 58
 li a0, 0
 li a1, 0
 li a2, 0
 li a3, 0
 li a4, 0
 li a5, 0
 li a6, 0
 mv a0, s0
 li a7, SYS_START
 ecall
 beqz a0, 99f
 j spawn_bad
99:
 beqz t0, 99f
 j spawn_bad
99:
 beqz a1, 99f
 j spawn_bad
99:
 beqz a2, 99f
 j spawn_bad
99:
 beqz a3, 99f
 j spawn_bad
99:
 beqz a4, 99f
 j spawn_bad
99:
 beqz a5, 99f
 j spawn_bad
99:
 beqz a6, 99f
 j spawn_bad
99:
 beqz a7, 99f
 j spawn_bad
99:
 li s9, 59
 li a0, 0
 li a1, 0
 li a2, 0
 li a3, 0
 li a4, 0
 li a5, 0
 li a6, 0
 mv a0, s1
 li a1, 1
 li a7, SYS_WAIT
 ecall
 beqz a0, 99f
 j spawn_bad
99:
 beqz t0, 99f
 j spawn_bad
99:
 beqz a1, 99f
 j spawn_bad
99:
 beqz a2, 99f
 j spawn_bad
99:
 beqz a3, 99f
 j spawn_bad
99:
 beqz a4, 99f
 j spawn_bad
99:
 beqz a5, 99f
 j spawn_bad
99:
 beqz a6, 99f
 j spawn_bad
99:
 beqz a7, 99f
 j spawn_bad
99:
 li s9, 60
 li a0, 0
 li a1, 0
 li a2, 0
 li a3, 0
 li a4, 0
 li a5, 0
 li a6, 0
 li a1, 1
 li a7, SYS_WAIT
 ecall
 beqz a0, 99f
 j spawn_bad
99:
 beqz t0, 99f
 j spawn_bad
99:
 beqz a1, 99f
 j spawn_bad
99:
 beqz a2, 99f
 j spawn_bad
99:
 beqz a3, 99f
 j spawn_bad
99:
 beqz a4, 99f
 j spawn_bad
99:
 beqz a5, 99f
 j spawn_bad
99:
 beqz a6, 99f
 j spawn_bad
99:
 beqz a7, 99f
 j spawn_bad
99:
 li s9, 61
 li a0, 0
 li a1, 0
 li a2, 0
 li a3, 0
 li a4, 0
 li a5, 0
 li a6, 0
 mv a0, s0
 li a7, SYS_DESTROY
 ecall
 li t1, -3
 beq a0, t1, 99f
 j spawn_bad
99:
 beqz t0, 99f
 j spawn_bad
99:
 beqz a1, 99f
 j spawn_bad
99:
 beqz a2, 99f
 j spawn_bad
99:
 beqz a3, 99f
 j spawn_bad
99:
 beqz a4, 99f
 j spawn_bad
99:
 beqz a5, 99f
 j spawn_bad
99:
 beqz a6, 99f
 j spawn_bad
99:
 beqz a7, 99f
 j spawn_bad
99:
 li s9, 62
 li a0, 0
 li a1, 0
 li a2, 0
 li a3, 0
 li a4, 0
 li a5, 0
 li a6, 0
 mv a0, s0
 li a1, 5
 li a3, 263168
 li a4, 1
 li a7, SYS_QUERY
 ecall
 li t1, -3
 beq a0, t1, 99f
 j spawn_bad
99:
 beqz t0, 99f
 j spawn_bad
99:
 beqz a1, 99f
 j spawn_bad
99:
 beqz a2, 99f
 j spawn_bad
99:
 beqz a3, 99f
 j spawn_bad
99:
 beqz a4, 99f
 j spawn_bad
99:
 beqz a5, 99f
 j spawn_bad
99:
 beqz a6, 99f
 j spawn_bad
99:
 beqz a7, 99f
 j spawn_bad
99:
 li s9, 63
 li a0, 0
 li a1, 0
 li a2, 0
 li a3, 0
 li a4, 0
 li a5, 0
 li a6, 0
 mv a0, s4
 li a1, 123
 li a5, 1
 li a7, SYS_CALL
 ecall
 beqz a0, 99f
 j spawn_bad
99:
 beqz t0, 99f
 j spawn_bad
99:
 beqz a1, 99f
 j spawn_bad
99:
 beqz a2, 99f
 j spawn_bad
99:
 beqz a4, 99f
 j spawn_bad
99:
 beqz a5, 99f
 j spawn_bad
99:
 beqz a6, 99f
 j spawn_bad
99:
 beqz a7, 99f
 j spawn_bad
99:
 mv s8, a3
 fmv.x.d t0, f31
 li t1, 4660
 beq t0, t1, 99f
 j spawn_bad
99:
 li s9, 64
 li a0, 0
 li a1, 0
 li a2, 0
 li a3, 0
 li a4, 0
 li a5, 0
 li a6, 0
 li a7, SYS_WAIT
 ecall
 beqz a0, 99f
 j spawn_bad
99:
 beqz t0, 99f
 j spawn_bad
99:
 mv t1, s1
 beq a1, t1, 99f
 j spawn_bad
99:
 li t1, 1
 beq a2, t1, 99f
 j spawn_bad
99:
 li t1, 42
 beq a3, t1, 99f
 j spawn_bad
99:
 li t1, 2
 beq a4, t1, 99f
 j spawn_bad
99:
 beqz a5, 99f
 j spawn_bad
99:
 beqz a6, 99f
 j spawn_bad
99:
 beqz a7, 99f
 j spawn_bad
99:
 li s9, 65
 li a0, 0
 li a1, 0
 li a2, 0
 li a3, 0
 li a4, 0
 li a5, 0
 li a6, 0
 li a1, 8
 li a5, 3
 li a7, SYS_RECV
 ecall
 beqz a0, 99f
 j spawn_bad
99:
 beqz t0, 99f
 j spawn_bad
99:
 li t1, 3
 beq a1, t1, 99f
 j spawn_bad
99:
 beqz a2, 99f
 j spawn_bad
99:
 beqz a3, 99f
 j spawn_bad
99:
 li t1, 8
 beq a4, t1, 99f
 j spawn_bad
99:
 beqz a5, 99f
 j spawn_bad
99:
 beqz a6, 99f
 j spawn_bad
99:
 beqz a7, 99f
 j spawn_bad
99:
 li s9, 66
 li a0, 0
 li a1, 0
 li a2, 0
 li a3, 0
 li a4, 0
 li a5, 0
 li a6, 0
 li a1, 8
 li a5, 3
 li a7, SYS_RECV
 ecall
 li t1, -11
 beq a0, t1, 99f
 j spawn_bad
99:
 beqz t0, 99f
 j spawn_bad
99:
 beqz a1, 99f
 j spawn_bad
99:
 beqz a2, 99f
 j spawn_bad
99:
 beqz a3, 99f
 j spawn_bad
99:
 beqz a4, 99f
 j spawn_bad
99:
 beqz a5, 99f
 j spawn_bad
99:
 beqz a6, 99f
 j spawn_bad
99:
 beqz a7, 99f
 j spawn_bad
99:
 li s9, 67
 li a0, 0
 li a1, 0
 li a2, 0
 li a3, 0
 li a4, 0
 li a5, 0
 li a6, 0
 mv a0, s8
 li a7, SYS_RQPOLL
 ecall
 beqz a0, 99f
 j spawn_bad
99:
 beqz t0, 99f
 j spawn_bad
99:
 beqz a1, 99f
 j spawn_bad
99:
 beqz a2, 99f
 j spawn_bad
99:
 beqz a3, 99f
 j spawn_bad
99:
 li t1, 321
 beq a4, t1, 99f
 j spawn_bad
99:
 beqz a5, 99f
 j spawn_bad
99:
 beqz a6, 99f
 j spawn_bad
99:
 beqz a7, 99f
 j spawn_bad
99:
 li s9, 68
 li a0, 0
 li a1, 0
 li a2, 0
 li a3, 0
 li a4, 0
 li a5, 0
 li a6, 0
 mv a0, s8
 li a1, 3
 li a7, SYS_RQACK
 ecall
 beqz a0, 99f
 j spawn_bad
99:
 beqz t0, 99f
 j spawn_bad
99:
 beqz a1, 99f
 j spawn_bad
99:
 beqz a2, 99f
 j spawn_bad
99:
 beqz a3, 99f
 j spawn_bad
99:
 beqz a4, 99f
 j spawn_bad
99:
 beqz a5, 99f
 j spawn_bad
99:
 beqz a6, 99f
 j spawn_bad
99:
 beqz a7, 99f
 j spawn_bad
99:
 li s9, 69
 li a0, 0
 li a1, 0
 li a2, 0
 li a3, 0
 li a4, 0
 li a5, 0
 li a6, 0
 mv a0, s1
 li a1, 1
 li a7, SYS_WAIT
 ecall
 li t1, -22
 beq a0, t1, 99f
 j spawn_bad
99:
 beqz t0, 99f
 j spawn_bad
99:
 beqz a1, 99f
 j spawn_bad
99:
 beqz a2, 99f
 j spawn_bad
99:
 beqz a3, 99f
 j spawn_bad
99:
 beqz a4, 99f
 j spawn_bad
99:
 beqz a5, 99f
 j spawn_bad
99:
 beqz a6, 99f
 j spawn_bad
99:
 beqz a7, 99f
 j spawn_bad
99:
 li s9, 70
 li a0, 0
 li a1, 0
 li a2, 0
 li a3, 0
 li a4, 0
 li a5, 0
 li a6, 0
 li a0, -9223372036854775808
 li a1, 1
 li a7, SYS_WAIT
 ecall
 li t1, -1
 beq a0, t1, 99f
 j spawn_bad
99:
 beqz t0, 99f
 j spawn_bad
99:
 beqz a1, 99f
 j spawn_bad
99:
 beqz a2, 99f
 j spawn_bad
99:
 beqz a3, 99f
 j spawn_bad
99:
 beqz a4, 99f
 j spawn_bad
99:
 beqz a5, 99f
 j spawn_bad
99:
 beqz a6, 99f
 j spawn_bad
99:
 beqz a7, 99f
 j spawn_bad
99:
 li s9, 71
 li a0, 0
 li a1, 0
 li a2, 0
 li a3, 0
 li a4, 0
 li a5, 0
 li a6, 0
 li a0, -1
 li a1, 8
 li a7, SYS_WAIT
 ecall
 li t1, -1
 beq a0, t1, 99f
 j spawn_bad
99:
 beqz t0, 99f
 j spawn_bad
99:
 beqz a1, 99f
 j spawn_bad
99:
 beqz a2, 99f
 j spawn_bad
99:
 beqz a3, 99f
 j spawn_bad
99:
 beqz a4, 99f
 j spawn_bad
99:
 beqz a5, 99f
 j spawn_bad
99:
 beqz a6, 99f
 j spawn_bad
99:
 beqz a7, 99f
 j spawn_bad
99:
 li s9, 72
 li a0, 0
 li a1, 0
 li a2, 0
 li a3, 0
 li a4, 0
 li a5, 0
 li a6, 0
 mv a0, s4
 li a5, 1
 li a7, SYS_CALL
 ecall
 li t1, -14
 beq a0, t1, 99f
 j spawn_bad
99:
 beqz t0, 99f
 j spawn_bad
99:
 beqz a1, 99f
 j spawn_bad
99:
 beqz a2, 99f
 j spawn_bad
99:
 beqz a3, 99f
 j spawn_bad
99:
 beqz a4, 99f
 j spawn_bad
99:
 beqz a5, 99f
 j spawn_bad
99:
 beqz a6, 99f
 j spawn_bad
99:
 beqz a7, 99f
 j spawn_bad
99:
 li t0, 262400
 li t1, 64
 sd t1, 0(t0)
 li t1, 128
 sd t1, 8(t0)
 li t1, 4294967304
 sd t1, 16(t0)
 li t1, 944892805330
 sd t1, 24(t0)
 li t1, 1030792151270
 sd t1, 32(t0)
 li t1, 0
 sd t1, 40(t0)
 li t0, 262144
 ld t2, 8(t0)
 li s9, 73
 li a0, 0
 li a1, 0
 li a2, 0
 li a3, 0
 li a4, 0
 li a5, 0
 li a6, 0
 mv a0, t2
 li a1, 262400
 li a7, SYS_CREATE
 ecall
 beqz a0, 99f
 j spawn_bad
99:
 beqz t0, 99f
 j spawn_bad
99:
 beqz a3, 99f
 j spawn_bad
99:
 beqz a4, 99f
 j spawn_bad
99:
 beqz a5, 99f
 j spawn_bad
99:
 beqz a6, 99f
 j spawn_bad
99:
 beqz a7, 99f
 j spawn_bad
99:
 mv s0, a1
 mv s1, a2
 mv s2, a3
 li t0, 262296
 sd s1, 0(t0)
 li s9, 74
 li a0, 0
 li a1, 0
 li a2, 0
 li a3, 0
 li a4, 0
 li a5, 0
 li a6, 0
 mv a0, s0
 li a7, SYS_START
 ecall
 li t1, -6
 beq a0, t1, 99f
 j spawn_bad
99:
 beqz t0, 99f
 j spawn_bad
99:
 beqz a1, 99f
 j spawn_bad
99:
 beqz a2, 99f
 j spawn_bad
99:
 beqz a3, 99f
 j spawn_bad
99:
 beqz a4, 99f
 j spawn_bad
99:
 beqz a5, 99f
 j spawn_bad
99:
 beqz a6, 99f
 j spawn_bad
99:
 beqz a7, 99f
 j spawn_bad
99:
 li s9, 75
 li a0, 0
 li a1, 0
 li a2, 0
 li a3, 0
 li a4, 0
 li a5, 0
 li a6, 0
 mv a0, s0
 li a1, 5
 li a2, 9223372036854775809
 li a7, SYS_CONNECT
 ecall
 li t1, -5
 beq a0, t1, 99f
 j spawn_bad
99:
 beqz t0, 99f
 j spawn_bad
99:
 beqz a1, 99f
 j spawn_bad
99:
 beqz a2, 99f
 j spawn_bad
99:
 beqz a3, 99f
 j spawn_bad
99:
 beqz a4, 99f
 j spawn_bad
99:
 beqz a5, 99f
 j spawn_bad
99:
 beqz a6, 99f
 j spawn_bad
99:
 beqz a7, 99f
 j spawn_bad
99:
 li s9, 76
 li a0, 0
 li a1, 0
 li a2, 0
 li a3, 0
 li a4, 0
 li a5, 0
 li a6, 0
 mv a0, s0
 li a1, 5
 li a2, 1
 li a7, SYS_CONNECT
 ecall
 beqz a0, 99f
 j spawn_bad
99:
 beqz t0, 99f
 j spawn_bad
99:
 beqz a2, 99f
 j spawn_bad
99:
 beqz a3, 99f
 j spawn_bad
99:
 beqz a4, 99f
 j spawn_bad
99:
 beqz a5, 99f
 j spawn_bad
99:
 beqz a6, 99f
 j spawn_bad
99:
 beqz a7, 99f
 j spawn_bad
99:
 mv s4, a1
 li s9, 77
 li a0, 0
 li a1, 0
 li a2, 0
 li a3, 0
 li a4, 0
 li a5, 0
 li a6, 0
 mv a0, s4
 li a5, 1
 li a7, SYS_CALL
 ecall
 li t1, -6
 beq a0, t1, 99f
 j spawn_bad
99:
 beqz t0, 99f
 j spawn_bad
99:
 beqz a1, 99f
 j spawn_bad
99:
 beqz a2, 99f
 j spawn_bad
99:
 beqz a3, 99f
 j spawn_bad
99:
 beqz a4, 99f
 j spawn_bad
99:
 beqz a5, 99f
 j spawn_bad
99:
 beqz a6, 99f
 j spawn_bad
99:
 beqz a7, 99f
 j spawn_bad
99:
 li t0, 262400
 li t1, 4096
 sd t1, 0(t0)
 li t1, 3
 sd t1, 8(t0)
 li t1, 0
 sd t1, 16(t0)
 li t1, 0
 sd t1, 24(t0)
 li t1, 0
 sd t1, 32(t0)
 li t1, 0
 sd t1, 40(t0)
 li s9, 78
 li a0, 0
 li a1, 0
 li a2, 0
 li a3, 0
 li a4, 0
 li a5, 0
 li a6, 0
 mv a0, s0
 li a1, 262400
 li a7, SYS_ALLOC
 ecall
 beqz a0, 99f
 j spawn_bad
99:
 beqz t0, 99f
 j spawn_bad
99:
 beqz a2, 99f
 j spawn_bad
99:
 beqz a3, 99f
 j spawn_bad
99:
 beqz a4, 99f
 j spawn_bad
99:
 beqz a5, 99f
 j spawn_bad
99:
 beqz a6, 99f
 j spawn_bad
99:
 beqz a7, 99f
 j spawn_bad
99:
 mv s3, a1
 li t0, 262144
 ld t2, 32(t0)
 li t0, 262464
 mv t1, s3
 sd t1, 0(t0)
 li t1, 0
 sd t1, 8(t0)
 li t1, 327680
 sd t1, 16(t0)
 li t1, 4096
 sd t1, 24(t0)
 li t1, 3
 sd t1, 32(t0)
 li t1, 0
 sd t1, 40(t0)
 li s9, 79
 li a0, 0
 li a1, 0
 li a2, 0
 li a3, 0
 li a4, 0
 li a5, 0
 li a6, 0
 mv a0, t2
 li a1, 262464
 li a7, SYS_MAP
 ecall
 beqz a0, 99f
 j spawn_bad
99:
 beqz t0, 99f
 j spawn_bad
99:
 beqz a1, 99f
 j spawn_bad
99:
 beqz a2, 99f
 j spawn_bad
99:
 beqz a3, 99f
 j spawn_bad
99:
 beqz a4, 99f
 j spawn_bad
99:
 beqz a5, 99f
 j spawn_bad
99:
 beqz a6, 99f
 j spawn_bad
99:
 beqz a7, 99f
 j spawn_bad
99:
 lla t0, spawn_child_begin
 li t1, 327680
 lla t3, spawn_child_end
spawn_copy_1:
 ld t2, 0(t0)
 sd t2, 0(t1)
 addi t0, t0, 8
 addi t1, t1, 8
 bltu t0, t3, spawn_copy_1
 lla t2, spawn_child_params
 lla t3, spawn_child_begin
 sub t2, t2, t3
 li t0, 327680
 add t0, t0, t2
 sd s1, 0(t0)
 li t0, 262144
 ld t2, 48(t0)
 lla t1, spawn_child_params
 lla t3, spawn_child_begin
 sub t1, t1, t3
 li t0, 327680
 add t0, t0, t1
 sd t2, 8(t0)
 li t1, 210
 sd t1, 16(t0)
 li t1, 220
 sd t1, 24(t0)
 li t1, 230
 sd t1, 32(t0)
 li t1, 240
 sd t1, 40(t0)
 li s9, 80
 li a0, 0
 li a1, 0
 li a2, 0
 li a3, 0
 li a4, 0
 li a5, 0
 li a6, 0
 mv a0, s3
 li a7, SYS_SEAL
 ecall
 li t1, -7
 beq a0, t1, 99f
 j spawn_bad
99:
 beqz t0, 99f
 j spawn_bad
99:
 beqz a1, 99f
 j spawn_bad
99:
 beqz a2, 99f
 j spawn_bad
99:
 beqz a3, 99f
 j spawn_bad
99:
 beqz a4, 99f
 j spawn_bad
99:
 beqz a5, 99f
 j spawn_bad
99:
 beqz a6, 99f
 j spawn_bad
99:
 beqz a7, 99f
 j spawn_bad
99:
 li t0, 262144
 ld t2, 32(t0)
 li s9, 81
 li a0, 0
 li a1, 0
 li a2, 0
 li a3, 0
 li a4, 0
 li a5, 0
 li a6, 0
 mv a0, t2
 li a1, 327680
 li a7, SYS_UNMAP
 ecall
 beqz a0, 99f
 j spawn_bad
99:
 beqz t0, 99f
 j spawn_bad
99:
 beqz a1, 99f
 j spawn_bad
99:
 beqz a2, 99f
 j spawn_bad
99:
 beqz a3, 99f
 j spawn_bad
99:
 beqz a4, 99f
 j spawn_bad
99:
 beqz a5, 99f
 j spawn_bad
99:
 beqz a6, 99f
 j spawn_bad
99:
 beqz a7, 99f
 j spawn_bad
99:
 li s9, 82
 li a0, 0
 li a1, 0
 li a2, 0
 li a3, 0
 li a4, 0
 li a5, 0
 li a6, 0
 mv a0, s3
 li a7, SYS_SEAL
 ecall
 beqz a0, 99f
 j spawn_bad
99:
 beqz t0, 99f
 j spawn_bad
99:
 beqz a1, 99f
 j spawn_bad
99:
 beqz a2, 99f
 j spawn_bad
99:
 beqz a3, 99f
 j spawn_bad
99:
 beqz a4, 99f
 j spawn_bad
99:
 beqz a5, 99f
 j spawn_bad
99:
 beqz a6, 99f
 j spawn_bad
99:
 beqz a7, 99f
 j spawn_bad
99:
 li t0, 262464
 mv t1, s3
 sd t1, 0(t0)
 li t1, 0
 sd t1, 8(t0)
 li t1, 65536
 sd t1, 16(t0)
 li t1, 4096
 sd t1, 24(t0)
 li t1, 5
 sd t1, 32(t0)
 li t1, 0
 sd t1, 40(t0)
 li s9, 83
 li a0, 0
 li a1, 0
 li a2, 0
 li a3, 0
 li a4, 0
 li a5, 0
 li a6, 0
 mv a0, s0
 li a1, 262464
 li a7, SYS_MAP
 ecall
 beqz a0, 99f
 j spawn_bad
99:
 beqz t0, 99f
 j spawn_bad
99:
 beqz a1, 99f
 j spawn_bad
99:
 beqz a2, 99f
 j spawn_bad
99:
 beqz a3, 99f
 j spawn_bad
99:
 beqz a4, 99f
 j spawn_bad
99:
 beqz a5, 99f
 j spawn_bad
99:
 beqz a6, 99f
 j spawn_bad
99:
 beqz a7, 99f
 j spawn_bad
99:
 li s9, 84
 li a0, 0
 li a1, 0
 li a2, 0
 li a3, 0
 li a4, 0
 li a5, 0
 li a6, 0
 mv a0, s0
 mv a1, s3
 li a2, 5
 li a4, 4096
 li a5, 1
 li a7, SYS_GRANT
 ecall
 beqz a0, 99f
 j spawn_bad
99:
 beqz t0, 99f
 j spawn_bad
99:
 beqz a2, 99f
 j spawn_bad
99:
 beqz a3, 99f
 j spawn_bad
99:
 beqz a4, 99f
 j spawn_bad
99:
 beqz a5, 99f
 j spawn_bad
99:
 beqz a6, 99f
 j spawn_bad
99:
 beqz a7, 99f
 j spawn_bad
99:
 li t0, 262400
 li t1, 65536
 sd t1, 0(t0)
 li t1, 1
 sd t1, 8(t0)
 li t1, 0
 sd t1, 16(t0)
 li t1, 0
 sd t1, 24(t0)
 li t1, 0
 sd t1, 32(t0)
 li t1, 0
 sd t1, 40(t0)
 li s9, 85
 li a0, 0
 li a1, 0
 li a2, 0
 li a3, 0
 li a4, 0
 li a5, 0
 li a6, 0
 mv a0, s0
 li a1, 262400
 li a7, SYS_ALLOC
 ecall
 beqz a0, 99f
 j spawn_bad
99:
 beqz t0, 99f
 j spawn_bad
99:
 beqz a2, 99f
 j spawn_bad
99:
 beqz a3, 99f
 j spawn_bad
99:
 beqz a4, 99f
 j spawn_bad
99:
 beqz a5, 99f
 j spawn_bad
99:
 beqz a6, 99f
 j spawn_bad
99:
 beqz a7, 99f
 j spawn_bad
99:
 mv s5, a1
 li t0, 262464
 mv t1, s5
 sd t1, 0(t0)
 li t1, 0
 sd t1, 8(t0)
 li t1, 274877775872
 sd t1, 16(t0)
 li t1, 65536
 sd t1, 24(t0)
 li t1, 3
 sd t1, 32(t0)
 li t1, 0
 sd t1, 40(t0)
 li s9, 86
 li a0, 0
 li a1, 0
 li a2, 0
 li a3, 0
 li a4, 0
 li a5, 0
 li a6, 0
 mv a0, s0
 li a1, 262464
 li a7, SYS_MAP
 ecall
 beqz a0, 99f
 j spawn_bad
99:
 beqz t0, 99f
 j spawn_bad
99:
 beqz a1, 99f
 j spawn_bad
99:
 beqz a2, 99f
 j spawn_bad
99:
 beqz a3, 99f
 j spawn_bad
99:
 beqz a4, 99f
 j spawn_bad
99:
 beqz a5, 99f
 j spawn_bad
99:
 beqz a6, 99f
 j spawn_bad
99:
 beqz a7, 99f
 j spawn_bad
99:
 li t0, 262400
 li t1, 4096
 sd t1, 0(t0)
 li t1, 1
 sd t1, 8(t0)
 li t1, 0
 sd t1, 16(t0)
 li t1, 0
 sd t1, 24(t0)
 li t1, 0
 sd t1, 32(t0)
 li t1, 0
 sd t1, 40(t0)
 li s9, 87
 li a0, 0
 li a1, 0
 li a2, 0
 li a3, 0
 li a4, 0
 li a5, 0
 li a6, 0
 mv a0, s0
 li a1, 262400
 li a7, SYS_ALLOC
 ecall
 beqz a0, 99f
 j spawn_bad
99:
 beqz t0, 99f
 j spawn_bad
99:
 beqz a2, 99f
 j spawn_bad
99:
 beqz a3, 99f
 j spawn_bad
99:
 beqz a4, 99f
 j spawn_bad
99:
 beqz a5, 99f
 j spawn_bad
99:
 beqz a6, 99f
 j spawn_bad
99:
 beqz a7, 99f
 j spawn_bad
99:
 mv s6, a1
 li t0, 262144
 ld t2, 32(t0)
 li t0, 262464
 mv t1, s6
 sd t1, 0(t0)
 li t1, 0
 sd t1, 8(t0)
 li t1, 327680
 sd t1, 16(t0)
 li t1, 4096
 sd t1, 24(t0)
 li t1, 3
 sd t1, 32(t0)
 li t1, 0
 sd t1, 40(t0)
 li s9, 88
 li a0, 0
 li a1, 0
 li a2, 0
 li a3, 0
 li a4, 0
 li a5, 0
 li a6, 0
 mv a0, t2
 li a1, 262464
 li a7, SYS_MAP
 ecall
 beqz a0, 99f
 j spawn_bad
99:
 beqz t0, 99f
 j spawn_bad
99:
 beqz a1, 99f
 j spawn_bad
99:
 beqz a2, 99f
 j spawn_bad
99:
 beqz a3, 99f
 j spawn_bad
99:
 beqz a4, 99f
 j spawn_bad
99:
 beqz a5, 99f
 j spawn_bad
99:
 beqz a6, 99f
 j spawn_bad
99:
 beqz a7, 99f
 j spawn_bad
99:
 li s9, 89
 li a0, 0
 li a1, 0
 li a2, 0
 li a3, 0
 li a4, 0
 li a5, 0
 li a6, 0
 mv a0, s0
 li a1, 5
 li a3, 263168
 li a4, 4
 li a7, SYS_QUERY
 ecall
 beqz a0, 99f
 j spawn_bad
99:
 beqz t0, 99f
 j spawn_bad
99:
 li t1, 4
 beq a1, t1, 99f
 j spawn_bad
99:
 beqz a2, 99f
 j spawn_bad
99:
 beqz a3, 99f
 j spawn_bad
99:
 beqz a4, 99f
 j spawn_bad
99:
 beqz a5, 99f
 j spawn_bad
99:
 beqz a6, 99f
 j spawn_bad
99:
 beqz a7, 99f
 j spawn_bad
99:
 li t0, 263168
 ld t2, 8(t0)
 mv t1, s1
 beq t2, t1, 99f
 j spawn_bad
99:
 li t0, 263168
 ld t2, 16(t0)
 li t1, 1
 beq t2, t1, 99f
 j spawn_bad
99:
 li t0, 263168
 ld t2, 40(t0)
 li t1, 2
 beq t2, t1, 99f
 j spawn_bad
99:
 li t0, 263168
 ld t2, 200(t0)
 mv t1, s1
 beq t2, t1, 99f
 j spawn_bad
99:
 li t0, 263168
 ld t2, 208(t0)
 li t1, 1
 beq t2, t1, 99f
 j spawn_bad
99:
 li t0, 263168
 ld t2, 232(t0)
 li t1, 3
 beq t2, t1, 99f
 j spawn_bad
99:
 li t0, 263168
 ld t2, 392(t0)
 mv t1, s1
 beq t2, t1, 99f
 j spawn_bad
99:
 li t0, 263168
 ld t2, 400(t0)
 li t1, 1
 beq t2, t1, 99f
 j spawn_bad
99:
 li t0, 263168
 ld t2, 424(t0)
 li t1, 1
 beq t2, t1, 99f
 j spawn_bad
99:
 li t0, 263168
 ld t2, 584(t0)
 mv t1, s1
 beq t2, t1, 99f
 j spawn_bad
99:
 li t0, 263168
 ld t2, 592(t0)
 li t1, 1
 beq t2, t1, 99f
 j spawn_bad
99:
 li t0, 263168
 ld t2, 616(t0)
 li t1, 4
 beq t2, t1, 99f
 j spawn_bad
99:
 li t0, 263168
 ld t2, 432(t0)
 li t1, 1
 beq t2, t1, 99f
 j spawn_bad
99:
 li t0, 327680
 li t1, 5422984103440953393
 sd t1, 0(t0)
 li t1, 1
 sd t1, 8(t0)
 li t1, 224
 sd t1, 16(t0)
 li t1, 0
 sd t1, 24(t0)
 li t1, 0
 sd t1, 32(t0)
 li t1, 0
 sd t1, 40(t0)
 li t1, 0
 sd t1, 48(t0)
 li t1, 0
 sd t1, 56(t0)
 li t1, 3
 sd t1, 64(t0)
 li t1, 128
 sd t1, 72(t0)
 li t1, 0
 sd t1, 80(t0)
 li t1, 0
 sd t1, 88(t0)
 li t1, 0
 sd t1, 96(t0)
 li t1, 0
 sd t1, 104(t0)
 li t1, 65536
 sd t1, 112(t0)
 li t1, 274877906944
 sd t1, 120(t0)
 li t0, 263168
 ld t2, 32(t0)
 li t0, 327808
 li t1, 1
 sd t1, 0(t0)
 sd t2, 8(t0)
 li t1, 0
 sd t1, 16(t0)
 sd t1, 24(t0)
 li t0, 263168
 ld t2, 224(t0)
 li t0, 327840
 li t1, 2
 sd t1, 0(t0)
 sd t2, 8(t0)
 li t1, 0
 sd t1, 16(t0)
 sd t1, 24(t0)
 li t0, 263168
 ld t2, 416(t0)
 li t0, 327872
 li t1, 3
 sd t1, 0(t0)
 sd t2, 8(t0)
 li t1, 0
 sd t1, 16(t0)
 sd t1, 24(t0)
 li t0, 262464
 mv t1, s6
 sd t1, 0(t0)
 li t1, 0
 sd t1, 8(t0)
 li t1, 274861129728
 sd t1, 16(t0)
 li t1, 4096
 sd t1, 24(t0)
 li t1, 1
 sd t1, 32(t0)
 li t1, 0
 sd t1, 40(t0)
 li s9, 90
 li a0, 0
 li a1, 0
 li a2, 0
 li a3, 0
 li a4, 0
 li a5, 0
 li a6, 0
 mv a0, s0
 li a1, 262464
 li a7, SYS_MAP
 ecall
 beqz a0, 99f
 j spawn_bad
99:
 beqz t0, 99f
 j spawn_bad
99:
 beqz a1, 99f
 j spawn_bad
99:
 beqz a2, 99f
 j spawn_bad
99:
 beqz a3, 99f
 j spawn_bad
99:
 beqz a4, 99f
 j spawn_bad
99:
 beqz a5, 99f
 j spawn_bad
99:
 beqz a6, 99f
 j spawn_bad
99:
 beqz a7, 99f
 j spawn_bad
99:
 li t0, 262528
 li t1, 65536
 sd t1, 0(t0)
 li t1, 274877841408
 sd t1, 8(t0)
 li t1, 274861129728
 sd t1, 16(t0)
 li t1, 224
 sd t1, 24(t0)
 li t1, 0
 sd t1, 32(t0)
 li t1, 0
 sd t1, 40(t0)
 li s9, 91
 li a0, 0
 li a1, 0
 li a2, 0
 li a3, 0
 li a4, 0
 li a5, 0
 li a6, 0
 mv a0, s0
 li a1, 262528
 li a2, 1
 li a7, SYS_CONFIGURE
 ecall
 li t1, -18
 beq a0, t1, 99f
 j spawn_bad
99:
 beqz t0, 99f
 j spawn_bad
99:
 beqz a1, 99f
 j spawn_bad
99:
 beqz a2, 99f
 j spawn_bad
99:
 beqz a3, 99f
 j spawn_bad
99:
 beqz a4, 99f
 j spawn_bad
99:
 beqz a5, 99f
 j spawn_bad
99:
 beqz a6, 99f
 j spawn_bad
99:
 beqz a7, 99f
 j spawn_bad
99:
 li s9, 92
 li a0, 0
 li a1, 0
 li a2, 0
 li a3, 0
 li a4, 0
 li a5, 0
 li a6, 0
 mv a0, s0
 li a1, 262528
 li a7, SYS_CONFIGURE
 ecall
 beqz a0, 99f
 j spawn_bad
99:
 beqz t0, 99f
 j spawn_bad
99:
 beqz a1, 99f
 j spawn_bad
99:
 beqz a2, 99f
 j spawn_bad
99:
 beqz a3, 99f
 j spawn_bad
99:
 beqz a4, 99f
 j spawn_bad
99:
 beqz a5, 99f
 j spawn_bad
99:
 beqz a6, 99f
 j spawn_bad
99:
 beqz a7, 99f
 j spawn_bad
99:
 li s9, 93
 li a0, 0
 li a1, 0
 li a2, 0
 li a3, 0
 li a4, 0
 li a5, 0
 li a6, 0
 mv a0, s0
 li a7, SYS_START
 ecall
 li t1, -7
 beq a0, t1, 99f
 j spawn_bad
99:
 beqz t0, 99f
 j spawn_bad
99:
 beqz a1, 99f
 j spawn_bad
99:
 beqz a2, 99f
 j spawn_bad
99:
 beqz a3, 99f
 j spawn_bad
99:
 beqz a4, 99f
 j spawn_bad
99:
 beqz a5, 99f
 j spawn_bad
99:
 beqz a6, 99f
 j spawn_bad
99:
 beqz a7, 99f
 j spawn_bad
99:
 li t0, 262144
 ld t2, 32(t0)
 li s9, 94
 li a0, 0
 li a1, 0
 li a2, 0
 li a3, 0
 li a4, 0
 li a5, 0
 li a6, 0
 mv a0, t2
 li a1, 327680
 li a7, SYS_UNMAP
 ecall
 beqz a0, 99f
 j spawn_bad
99:
 beqz t0, 99f
 j spawn_bad
99:
 beqz a1, 99f
 j spawn_bad
99:
 beqz a2, 99f
 j spawn_bad
99:
 beqz a3, 99f
 j spawn_bad
99:
 beqz a4, 99f
 j spawn_bad
99:
 beqz a5, 99f
 j spawn_bad
99:
 beqz a6, 99f
 j spawn_bad
99:
 beqz a7, 99f
 j spawn_bad
99:
 li s9, 95
 li a0, 0
 li a1, 0
 li a2, 0
 li a3, 0
 li a4, 0
 li a5, 0
 li a6, 0
 mv a0, s0
 li a7, SYS_START
 ecall
 beqz a0, 99f
 j spawn_bad
99:
 beqz t0, 99f
 j spawn_bad
99:
 beqz a1, 99f
 j spawn_bad
99:
 beqz a2, 99f
 j spawn_bad
99:
 beqz a3, 99f
 j spawn_bad
99:
 beqz a4, 99f
 j spawn_bad
99:
 beqz a5, 99f
 j spawn_bad
99:
 beqz a6, 99f
 j spawn_bad
99:
 beqz a7, 99f
 j spawn_bad
99:
 li s9, 96
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
 beqz a0, 99f
 j spawn_bad
99:
 beqz t0, 99f
 j spawn_bad
99:
 beqz a1, 99f
 j spawn_bad
99:
 beqz a2, 99f
 j spawn_bad
99:
 beqz a3, 99f
 j spawn_bad
99:
 beqz a4, 99f
 j spawn_bad
99:
 beqz a5, 99f
 j spawn_bad
99:
 beqz a6, 99f
 j spawn_bad
99:
 beqz a7, 99f
 j spawn_bad
99:
 neg s7, s1
 li s9, 97
 li a0, 0
 li a1, 0
 li a2, 0
 li a3, 0
 li a4, 0
 li a5, 0
 li a6, 0
 mv a0, s1
 li a1, 1
 li a7, SYS_WAIT
 ecall
 beqz a0, 99f
 j spawn_bad
99:
 beqz t0, 99f
 j spawn_bad
99:
 beqz a1, 99f
 j spawn_bad
99:
 beqz a2, 99f
 j spawn_bad
99:
 beqz a3, 99f
 j spawn_bad
99:
 beqz a4, 99f
 j spawn_bad
99:
 beqz a5, 99f
 j spawn_bad
99:
 beqz a6, 99f
 j spawn_bad
99:
 beqz a7, 99f
 j spawn_bad
99:
 li s9, 98
 li a0, 0
 li a1, 0
 li a2, 0
 li a3, 0
 li a4, 0
 li a5, 0
 li a6, 0
 li a1, 1
 li a7, SYS_WAIT
 ecall
 li t1, -22
 beq a0, t1, 99f
 j spawn_bad
99:
 beqz t0, 99f
 j spawn_bad
99:
 beqz a1, 99f
 j spawn_bad
99:
 beqz a2, 99f
 j spawn_bad
99:
 beqz a3, 99f
 j spawn_bad
99:
 beqz a4, 99f
 j spawn_bad
99:
 beqz a5, 99f
 j spawn_bad
99:
 beqz a6, 99f
 j spawn_bad
99:
 beqz a7, 99f
 j spawn_bad
99:
 li s9, 99
 li a0, 0
 li a1, 0
 li a2, 0
 li a3, 0
 li a4, 0
 li a5, 0
 li a6, 0
 mv a0, s0
 li a7, SYS_DESTROY
 ecall
 li t1, -3
 beq a0, t1, 99f
 j spawn_bad
99:
 beqz t0, 99f
 j spawn_bad
99:
 beqz a1, 99f
 j spawn_bad
99:
 beqz a2, 99f
 j spawn_bad
99:
 beqz a3, 99f
 j spawn_bad
99:
 beqz a4, 99f
 j spawn_bad
99:
 beqz a5, 99f
 j spawn_bad
99:
 beqz a6, 99f
 j spawn_bad
99:
 beqz a7, 99f
 j spawn_bad
99:
 li s9, 100
 li a0, 0
 li a1, 0
 li a2, 0
 li a3, 0
 li a4, 0
 li a5, 0
 li a6, 0
 mv a0, s0
 li a1, 5
 li a3, 263168
 li a4, 1
 li a7, SYS_QUERY
 ecall
 li t1, -3
 beq a0, t1, 99f
 j spawn_bad
99:
 beqz t0, 99f
 j spawn_bad
99:
 beqz a1, 99f
 j spawn_bad
99:
 beqz a2, 99f
 j spawn_bad
99:
 beqz a3, 99f
 j spawn_bad
99:
 beqz a4, 99f
 j spawn_bad
99:
 beqz a5, 99f
 j spawn_bad
99:
 beqz a6, 99f
 j spawn_bad
99:
 beqz a7, 99f
 j spawn_bad
99:
 li s9, 101
 li a0, 0
 li a1, 0
 li a2, 0
 li a3, 0
 li a4, 0
 li a5, 0
 li a6, 0
 mv a0, s4
 li a1, 123
 li a5, 1
 li a7, SYS_CALL
 ecall
 beqz a0, 99f
 j spawn_bad
99:
 beqz t0, 99f
 j spawn_bad
99:
 beqz a1, 99f
 j spawn_bad
99:
 beqz a2, 99f
 j spawn_bad
99:
 beqz a4, 99f
 j spawn_bad
99:
 beqz a5, 99f
 j spawn_bad
99:
 beqz a6, 99f
 j spawn_bad
99:
 beqz a7, 99f
 j spawn_bad
99:
 mv s8, a3
 fmv.x.d t0, f31
 li t1, 4660
 beq t0, t1, 99f
 j spawn_bad
99:
 li s9, 102
 li a0, 0
 li a1, 0
 li a2, 0
 li a3, 0
 li a4, 0
 li a5, 0
 li a6, 0
 mv a0, s7
 li a7, SYS_WAIT
 ecall
 beqz a0, 99f
 j spawn_bad
99:
 beqz t0, 99f
 j spawn_bad
99:
 mv t1, s1
 beq a1, t1, 99f
 j spawn_bad
99:
 li t1, 1
 beq a2, t1, 99f
 j spawn_bad
99:
 li t1, 42
 beq a3, t1, 99f
 j spawn_bad
99:
 li t1, 2
 beq a4, t1, 99f
 j spawn_bad
99:
 beqz a5, 99f
 j spawn_bad
99:
 beqz a6, 99f
 j spawn_bad
99:
 beqz a7, 99f
 j spawn_bad
99:
 li s9, 103
 li a0, 0
 li a1, 0
 li a2, 0
 li a3, 0
 li a4, 0
 li a5, 0
 li a6, 0
 li a1, 8
 li a5, 3
 li a7, SYS_RECV
 ecall
 beqz a0, 99f
 j spawn_bad
99:
 beqz t0, 99f
 j spawn_bad
99:
 li t1, 3
 beq a1, t1, 99f
 j spawn_bad
99:
 beqz a2, 99f
 j spawn_bad
99:
 beqz a3, 99f
 j spawn_bad
99:
 li t1, 8
 beq a4, t1, 99f
 j spawn_bad
99:
 beqz a5, 99f
 j spawn_bad
99:
 beqz a6, 99f
 j spawn_bad
99:
 beqz a7, 99f
 j spawn_bad
99:
 li s9, 104
 li a0, 0
 li a1, 0
 li a2, 0
 li a3, 0
 li a4, 0
 li a5, 0
 li a6, 0
 li a1, 8
 li a5, 3
 li a7, SYS_RECV
 ecall
 li t1, -11
 beq a0, t1, 99f
 j spawn_bad
99:
 beqz t0, 99f
 j spawn_bad
99:
 beqz a1, 99f
 j spawn_bad
99:
 beqz a2, 99f
 j spawn_bad
99:
 beqz a3, 99f
 j spawn_bad
99:
 beqz a4, 99f
 j spawn_bad
99:
 beqz a5, 99f
 j spawn_bad
99:
 beqz a6, 99f
 j spawn_bad
99:
 beqz a7, 99f
 j spawn_bad
99:
 li s9, 105
 li a0, 0
 li a1, 0
 li a2, 0
 li a3, 0
 li a4, 0
 li a5, 0
 li a6, 0
 mv a0, s8
 li a7, SYS_RQPOLL
 ecall
 beqz a0, 99f
 j spawn_bad
99:
 beqz t0, 99f
 j spawn_bad
99:
 beqz a1, 99f
 j spawn_bad
99:
 beqz a2, 99f
 j spawn_bad
99:
 beqz a3, 99f
 j spawn_bad
99:
 li t1, 321
 beq a4, t1, 99f
 j spawn_bad
99:
 beqz a5, 99f
 j spawn_bad
99:
 beqz a6, 99f
 j spawn_bad
99:
 beqz a7, 99f
 j spawn_bad
99:
 li s9, 106
 li a0, 0
 li a1, 0
 li a2, 0
 li a3, 0
 li a4, 0
 li a5, 0
 li a6, 0
 mv a0, s8
 li a1, 3
 li a7, SYS_RQACK
 ecall
 beqz a0, 99f
 j spawn_bad
99:
 beqz t0, 99f
 j spawn_bad
99:
 beqz a1, 99f
 j spawn_bad
99:
 beqz a2, 99f
 j spawn_bad
99:
 beqz a3, 99f
 j spawn_bad
99:
 beqz a4, 99f
 j spawn_bad
99:
 beqz a5, 99f
 j spawn_bad
99:
 beqz a6, 99f
 j spawn_bad
99:
 beqz a7, 99f
 j spawn_bad
99:
 li s9, 107
 li a0, 0
 li a1, 0
 li a2, 0
 li a3, 0
 li a4, 0
 li a5, 0
 li a6, 0
 mv a0, s1
 li a1, 1
 li a7, SYS_WAIT
 ecall
 li t1, -22
 beq a0, t1, 99f
 j spawn_bad
99:
 beqz t0, 99f
 j spawn_bad
99:
 beqz a1, 99f
 j spawn_bad
99:
 beqz a2, 99f
 j spawn_bad
99:
 beqz a3, 99f
 j spawn_bad
99:
 beqz a4, 99f
 j spawn_bad
99:
 beqz a5, 99f
 j spawn_bad
99:
 beqz a6, 99f
 j spawn_bad
99:
 beqz a7, 99f
 j spawn_bad
99:
 li s9, 108
 li a0, 0
 li a1, 0
 li a2, 0
 li a3, 0
 li a4, 0
 li a5, 0
 li a6, 0
 li a0, -9223372036854775808
 li a1, 1
 li a7, SYS_WAIT
 ecall
 li t1, -1
 beq a0, t1, 99f
 j spawn_bad
99:
 beqz t0, 99f
 j spawn_bad
99:
 beqz a1, 99f
 j spawn_bad
99:
 beqz a2, 99f
 j spawn_bad
99:
 beqz a3, 99f
 j spawn_bad
99:
 beqz a4, 99f
 j spawn_bad
99:
 beqz a5, 99f
 j spawn_bad
99:
 beqz a6, 99f
 j spawn_bad
99:
 beqz a7, 99f
 j spawn_bad
99:
 li s9, 109
 li a0, 0
 li a1, 0
 li a2, 0
 li a3, 0
 li a4, 0
 li a5, 0
 li a6, 0
 li a0, -1
 li a1, 8
 li a7, SYS_WAIT
 ecall
 li t1, -1
 beq a0, t1, 99f
 j spawn_bad
99:
 beqz t0, 99f
 j spawn_bad
99:
 beqz a1, 99f
 j spawn_bad
99:
 beqz a2, 99f
 j spawn_bad
99:
 beqz a3, 99f
 j spawn_bad
99:
 beqz a4, 99f
 j spawn_bad
99:
 beqz a5, 99f
 j spawn_bad
99:
 beqz a6, 99f
 j spawn_bad
99:
 beqz a7, 99f
 j spawn_bad
99:
 li s9, 110
 li a0, 0
 li a1, 0
 li a2, 0
 li a3, 0
 li a4, 0
 li a5, 0
 li a6, 0
 mv a0, s4
 li a5, 1
 li a7, SYS_CALL
 ecall
 li t1, -14
 beq a0, t1, 99f
 j spawn_bad
99:
 beqz t0, 99f
 j spawn_bad
99:
 beqz a1, 99f
 j spawn_bad
99:
 beqz a2, 99f
 j spawn_bad
99:
 beqz a3, 99f
 j spawn_bad
99:
 beqz a4, 99f
 j spawn_bad
99:
 beqz a5, 99f
 j spawn_bad
99:
 beqz a6, 99f
 j spawn_bad
99:
 beqz a7, 99f
 j spawn_bad
99:
 li t0, 262400
 li t1, 64
 sd t1, 0(t0)
 li t1, 128
 sd t1, 8(t0)
 li t1, 4294967304
 sd t1, 16(t0)
 li t1, 515396075630
 sd t1, 24(t0)
 li t1, 601295421570
 sd t1, 32(t0)
 li t1, 0
 sd t1, 40(t0)
 li t0, 262144
 ld t2, 16(t0)
 li s9, 111
 li a0, 0
 li a1, 0
 li a2, 0
 li a3, 0
 li a4, 0
 li a5, 0
 li a6, 0
 mv a0, t2
 li a1, 262400
 li a7, SYS_CREATE
 ecall
 beqz a0, 99f
 j spawn_bad
99:
 beqz t0, 99f
 j spawn_bad
99:
 beqz a4, 99f
 j spawn_bad
99:
 beqz a5, 99f
 j spawn_bad
99:
 beqz a6, 99f
 j spawn_bad
99:
 beqz a7, 99f
 j spawn_bad
99:
 mv s0, a1
 mv s1, a2
 mv s2, a3
 li t0, 262304
 sd s1, 0(t0)
 li s9, 112
 li a0, 0
 li a1, 0
 li a2, 0
 li a3, 0
 li a4, 0
 li a5, 0
 li a6, 0
 mv a0, s0
 li a7, SYS_START
 ecall
 li t1, -6
 beq a0, t1, 99f
 j spawn_bad
99:
 beqz t0, 99f
 j spawn_bad
99:
 beqz a1, 99f
 j spawn_bad
99:
 beqz a2, 99f
 j spawn_bad
99:
 beqz a3, 99f
 j spawn_bad
99:
 beqz a4, 99f
 j spawn_bad
99:
 beqz a5, 99f
 j spawn_bad
99:
 beqz a6, 99f
 j spawn_bad
99:
 beqz a7, 99f
 j spawn_bad
99:
 li s9, 113
 li a0, 0
 li a1, 0
 li a2, 0
 li a3, 0
 li a4, 0
 li a5, 0
 li a6, 0
 mv a0, s0
 li a1, 5
 li a2, 9223372036854775809
 li a7, SYS_CONNECT
 ecall
 li t1, -5
 beq a0, t1, 99f
 j spawn_bad
99:
 beqz t0, 99f
 j spawn_bad
99:
 beqz a1, 99f
 j spawn_bad
99:
 beqz a2, 99f
 j spawn_bad
99:
 beqz a3, 99f
 j spawn_bad
99:
 beqz a4, 99f
 j spawn_bad
99:
 beqz a5, 99f
 j spawn_bad
99:
 beqz a6, 99f
 j spawn_bad
99:
 beqz a7, 99f
 j spawn_bad
99:
 li s9, 114
 li a0, 0
 li a1, 0
 li a2, 0
 li a3, 0
 li a4, 0
 li a5, 0
 li a6, 0
 mv a0, s0
 li a1, 5
 li a2, 1
 li a7, SYS_CONNECT
 ecall
 beqz a0, 99f
 j spawn_bad
99:
 beqz t0, 99f
 j spawn_bad
99:
 beqz a2, 99f
 j spawn_bad
99:
 beqz a3, 99f
 j spawn_bad
99:
 beqz a4, 99f
 j spawn_bad
99:
 beqz a5, 99f
 j spawn_bad
99:
 beqz a6, 99f
 j spawn_bad
99:
 beqz a7, 99f
 j spawn_bad
99:
 mv s4, a1
 li s9, 115
 li a0, 0
 li a1, 0
 li a2, 0
 li a3, 0
 li a4, 0
 li a5, 0
 li a6, 0
 mv a0, s4
 li a5, 1
 li a7, SYS_CALL
 ecall
 li t1, -6
 beq a0, t1, 99f
 j spawn_bad
99:
 beqz t0, 99f
 j spawn_bad
99:
 beqz a1, 99f
 j spawn_bad
99:
 beqz a2, 99f
 j spawn_bad
99:
 beqz a3, 99f
 j spawn_bad
99:
 beqz a4, 99f
 j spawn_bad
99:
 beqz a5, 99f
 j spawn_bad
99:
 beqz a6, 99f
 j spawn_bad
99:
 beqz a7, 99f
 j spawn_bad
99:
 li t0, 262400
 li t1, 4096
 sd t1, 0(t0)
 li t1, 3
 sd t1, 8(t0)
 li t1, 0
 sd t1, 16(t0)
 li t1, 0
 sd t1, 24(t0)
 li t1, 0
 sd t1, 32(t0)
 li t1, 0
 sd t1, 40(t0)
 li s9, 116
 li a0, 0
 li a1, 0
 li a2, 0
 li a3, 0
 li a4, 0
 li a5, 0
 li a6, 0
 mv a0, s0
 li a1, 262400
 li a7, SYS_ALLOC
 ecall
 beqz a0, 99f
 j spawn_bad
99:
 beqz t0, 99f
 j spawn_bad
99:
 beqz a2, 99f
 j spawn_bad
99:
 beqz a3, 99f
 j spawn_bad
99:
 beqz a4, 99f
 j spawn_bad
99:
 beqz a5, 99f
 j spawn_bad
99:
 beqz a6, 99f
 j spawn_bad
99:
 beqz a7, 99f
 j spawn_bad
99:
 mv s3, a1
 li t0, 262144
 ld t2, 32(t0)
 li t0, 262464
 mv t1, s3
 sd t1, 0(t0)
 li t1, 0
 sd t1, 8(t0)
 li t1, 327680
 sd t1, 16(t0)
 li t1, 4096
 sd t1, 24(t0)
 li t1, 3
 sd t1, 32(t0)
 li t1, 0
 sd t1, 40(t0)
 li s9, 117
 li a0, 0
 li a1, 0
 li a2, 0
 li a3, 0
 li a4, 0
 li a5, 0
 li a6, 0
 mv a0, t2
 li a1, 262464
 li a7, SYS_MAP
 ecall
 beqz a0, 99f
 j spawn_bad
99:
 beqz t0, 99f
 j spawn_bad
99:
 beqz a1, 99f
 j spawn_bad
99:
 beqz a2, 99f
 j spawn_bad
99:
 beqz a3, 99f
 j spawn_bad
99:
 beqz a4, 99f
 j spawn_bad
99:
 beqz a5, 99f
 j spawn_bad
99:
 beqz a6, 99f
 j spawn_bad
99:
 beqz a7, 99f
 j spawn_bad
99:
 lla t0, spawn_child_begin
 li t1, 327680
 lla t3, spawn_child_end
spawn_copy_2:
 ld t2, 0(t0)
 sd t2, 0(t1)
 addi t0, t0, 8
 addi t1, t1, 8
 bltu t0, t3, spawn_copy_2
 lla t2, spawn_child_params
 lla t3, spawn_child_begin
 sub t2, t2, t3
 li t0, 327680
 add t0, t0, t2
 sd s1, 0(t0)
 li t0, 262144
 ld t2, 48(t0)
 lla t1, spawn_child_params
 lla t3, spawn_child_begin
 sub t1, t1, t3
 li t0, 327680
 add t0, t0, t1
 sd t2, 8(t0)
 li t1, 110
 sd t1, 16(t0)
 li t1, 120
 sd t1, 24(t0)
 li t1, 130
 sd t1, 32(t0)
 li t1, 140
 sd t1, 40(t0)
 li s9, 118
 li a0, 0
 li a1, 0
 li a2, 0
 li a3, 0
 li a4, 0
 li a5, 0
 li a6, 0
 mv a0, s3
 li a7, SYS_SEAL
 ecall
 li t1, -7
 beq a0, t1, 99f
 j spawn_bad
99:
 beqz t0, 99f
 j spawn_bad
99:
 beqz a1, 99f
 j spawn_bad
99:
 beqz a2, 99f
 j spawn_bad
99:
 beqz a3, 99f
 j spawn_bad
99:
 beqz a4, 99f
 j spawn_bad
99:
 beqz a5, 99f
 j spawn_bad
99:
 beqz a6, 99f
 j spawn_bad
99:
 beqz a7, 99f
 j spawn_bad
99:
 li t0, 262144
 ld t2, 32(t0)
 li s9, 119
 li a0, 0
 li a1, 0
 li a2, 0
 li a3, 0
 li a4, 0
 li a5, 0
 li a6, 0
 mv a0, t2
 li a1, 327680
 li a7, SYS_UNMAP
 ecall
 beqz a0, 99f
 j spawn_bad
99:
 beqz t0, 99f
 j spawn_bad
99:
 beqz a1, 99f
 j spawn_bad
99:
 beqz a2, 99f
 j spawn_bad
99:
 beqz a3, 99f
 j spawn_bad
99:
 beqz a4, 99f
 j spawn_bad
99:
 beqz a5, 99f
 j spawn_bad
99:
 beqz a6, 99f
 j spawn_bad
99:
 beqz a7, 99f
 j spawn_bad
99:
 li s9, 120
 li a0, 0
 li a1, 0
 li a2, 0
 li a3, 0
 li a4, 0
 li a5, 0
 li a6, 0
 mv a0, s3
 li a7, SYS_SEAL
 ecall
 beqz a0, 99f
 j spawn_bad
99:
 beqz t0, 99f
 j spawn_bad
99:
 beqz a1, 99f
 j spawn_bad
99:
 beqz a2, 99f
 j spawn_bad
99:
 beqz a3, 99f
 j spawn_bad
99:
 beqz a4, 99f
 j spawn_bad
99:
 beqz a5, 99f
 j spawn_bad
99:
 beqz a6, 99f
 j spawn_bad
99:
 beqz a7, 99f
 j spawn_bad
99:
 li t0, 262464
 mv t1, s3
 sd t1, 0(t0)
 li t1, 0
 sd t1, 8(t0)
 li t1, 65536
 sd t1, 16(t0)
 li t1, 4096
 sd t1, 24(t0)
 li t1, 5
 sd t1, 32(t0)
 li t1, 0
 sd t1, 40(t0)
 li s9, 121
 li a0, 0
 li a1, 0
 li a2, 0
 li a3, 0
 li a4, 0
 li a5, 0
 li a6, 0
 mv a0, s0
 li a1, 262464
 li a7, SYS_MAP
 ecall
 beqz a0, 99f
 j spawn_bad
99:
 beqz t0, 99f
 j spawn_bad
99:
 beqz a1, 99f
 j spawn_bad
99:
 beqz a2, 99f
 j spawn_bad
99:
 beqz a3, 99f
 j spawn_bad
99:
 beqz a4, 99f
 j spawn_bad
99:
 beqz a5, 99f
 j spawn_bad
99:
 beqz a6, 99f
 j spawn_bad
99:
 beqz a7, 99f
 j spawn_bad
99:
 li s9, 122
 li a0, 0
 li a1, 0
 li a2, 0
 li a3, 0
 li a4, 0
 li a5, 0
 li a6, 0
 mv a0, s0
 mv a1, s3
 li a2, 5
 li a4, 4096
 li a5, 1
 li a7, SYS_GRANT
 ecall
 beqz a0, 99f
 j spawn_bad
99:
 beqz t0, 99f
 j spawn_bad
99:
 beqz a2, 99f
 j spawn_bad
99:
 beqz a3, 99f
 j spawn_bad
99:
 beqz a4, 99f
 j spawn_bad
99:
 beqz a5, 99f
 j spawn_bad
99:
 beqz a6, 99f
 j spawn_bad
99:
 beqz a7, 99f
 j spawn_bad
99:
 li t0, 262400
 li t1, 65536
 sd t1, 0(t0)
 li t1, 1
 sd t1, 8(t0)
 li t1, 0
 sd t1, 16(t0)
 li t1, 0
 sd t1, 24(t0)
 li t1, 0
 sd t1, 32(t0)
 li t1, 0
 sd t1, 40(t0)
 li s9, 123
 li a0, 0
 li a1, 0
 li a2, 0
 li a3, 0
 li a4, 0
 li a5, 0
 li a6, 0
 mv a0, s0
 li a1, 262400
 li a7, SYS_ALLOC
 ecall
 beqz a0, 99f
 j spawn_bad
99:
 beqz t0, 99f
 j spawn_bad
99:
 beqz a2, 99f
 j spawn_bad
99:
 beqz a3, 99f
 j spawn_bad
99:
 beqz a4, 99f
 j spawn_bad
99:
 beqz a5, 99f
 j spawn_bad
99:
 beqz a6, 99f
 j spawn_bad
99:
 beqz a7, 99f
 j spawn_bad
99:
 mv s5, a1
 li t0, 262464
 mv t1, s5
 sd t1, 0(t0)
 li t1, 0
 sd t1, 8(t0)
 li t1, 274877775872
 sd t1, 16(t0)
 li t1, 65536
 sd t1, 24(t0)
 li t1, 3
 sd t1, 32(t0)
 li t1, 0
 sd t1, 40(t0)
 li s9, 124
 li a0, 0
 li a1, 0
 li a2, 0
 li a3, 0
 li a4, 0
 li a5, 0
 li a6, 0
 mv a0, s0
 li a1, 262464
 li a7, SYS_MAP
 ecall
 beqz a0, 99f
 j spawn_bad
99:
 beqz t0, 99f
 j spawn_bad
99:
 beqz a1, 99f
 j spawn_bad
99:
 beqz a2, 99f
 j spawn_bad
99:
 beqz a3, 99f
 j spawn_bad
99:
 beqz a4, 99f
 j spawn_bad
99:
 beqz a5, 99f
 j spawn_bad
99:
 beqz a6, 99f
 j spawn_bad
99:
 beqz a7, 99f
 j spawn_bad
99:
 li t0, 262400
 li t1, 4096
 sd t1, 0(t0)
 li t1, 1
 sd t1, 8(t0)
 li t1, 0
 sd t1, 16(t0)
 li t1, 0
 sd t1, 24(t0)
 li t1, 0
 sd t1, 32(t0)
 li t1, 0
 sd t1, 40(t0)
 li s9, 125
 li a0, 0
 li a1, 0
 li a2, 0
 li a3, 0
 li a4, 0
 li a5, 0
 li a6, 0
 mv a0, s0
 li a1, 262400
 li a7, SYS_ALLOC
 ecall
 beqz a0, 99f
 j spawn_bad
99:
 beqz t0, 99f
 j spawn_bad
99:
 beqz a2, 99f
 j spawn_bad
99:
 beqz a3, 99f
 j spawn_bad
99:
 beqz a4, 99f
 j spawn_bad
99:
 beqz a5, 99f
 j spawn_bad
99:
 beqz a6, 99f
 j spawn_bad
99:
 beqz a7, 99f
 j spawn_bad
99:
 mv s6, a1
 li t0, 262144
 ld t2, 32(t0)
 li t0, 262464
 mv t1, s6
 sd t1, 0(t0)
 li t1, 0
 sd t1, 8(t0)
 li t1, 327680
 sd t1, 16(t0)
 li t1, 4096
 sd t1, 24(t0)
 li t1, 3
 sd t1, 32(t0)
 li t1, 0
 sd t1, 40(t0)
 li s9, 126
 li a0, 0
 li a1, 0
 li a2, 0
 li a3, 0
 li a4, 0
 li a5, 0
 li a6, 0
 mv a0, t2
 li a1, 262464
 li a7, SYS_MAP
 ecall
 beqz a0, 99f
 j spawn_bad
99:
 beqz t0, 99f
 j spawn_bad
99:
 beqz a1, 99f
 j spawn_bad
99:
 beqz a2, 99f
 j spawn_bad
99:
 beqz a3, 99f
 j spawn_bad
99:
 beqz a4, 99f
 j spawn_bad
99:
 beqz a5, 99f
 j spawn_bad
99:
 beqz a6, 99f
 j spawn_bad
99:
 beqz a7, 99f
 j spawn_bad
99:
 li s9, 127
 li a0, 0
 li a1, 0
 li a2, 0
 li a3, 0
 li a4, 0
 li a5, 0
 li a6, 0
 mv a0, s0
 li a1, 5
 li a3, 263168
 li a4, 4
 li a7, SYS_QUERY
 ecall
 beqz a0, 99f
 j spawn_bad
99:
 beqz t0, 99f
 j spawn_bad
99:
 li t1, 4
 beq a1, t1, 99f
 j spawn_bad
99:
 beqz a2, 99f
 j spawn_bad
99:
 beqz a3, 99f
 j spawn_bad
99:
 beqz a4, 99f
 j spawn_bad
99:
 beqz a5, 99f
 j spawn_bad
99:
 beqz a6, 99f
 j spawn_bad
99:
 beqz a7, 99f
 j spawn_bad
99:
 li t0, 263168
 ld t2, 8(t0)
 mv t1, s1
 beq t2, t1, 99f
 j spawn_bad
99:
 li t0, 263168
 ld t2, 16(t0)
 li t1, 1
 beq t2, t1, 99f
 j spawn_bad
99:
 li t0, 263168
 ld t2, 40(t0)
 li t1, 2
 beq t2, t1, 99f
 j spawn_bad
99:
 li t0, 263168
 ld t2, 200(t0)
 mv t1, s1
 beq t2, t1, 99f
 j spawn_bad
99:
 li t0, 263168
 ld t2, 208(t0)
 li t1, 1
 beq t2, t1, 99f
 j spawn_bad
99:
 li t0, 263168
 ld t2, 232(t0)
 li t1, 3
 beq t2, t1, 99f
 j spawn_bad
99:
 li t0, 263168
 ld t2, 392(t0)
 mv t1, s1
 beq t2, t1, 99f
 j spawn_bad
99:
 li t0, 263168
 ld t2, 400(t0)
 li t1, 1
 beq t2, t1, 99f
 j spawn_bad
99:
 li t0, 263168
 ld t2, 424(t0)
 li t1, 1
 beq t2, t1, 99f
 j spawn_bad
99:
 li t0, 263168
 ld t2, 584(t0)
 mv t1, s1
 beq t2, t1, 99f
 j spawn_bad
99:
 li t0, 263168
 ld t2, 592(t0)
 li t1, 1
 beq t2, t1, 99f
 j spawn_bad
99:
 li t0, 263168
 ld t2, 616(t0)
 li t1, 4
 beq t2, t1, 99f
 j spawn_bad
99:
 li t0, 263168
 ld t2, 432(t0)
 li t1, 1
 beq t2, t1, 99f
 j spawn_bad
99:
 li t0, 327680
 li t1, 5422984103440953393
 sd t1, 0(t0)
 li t1, 1
 sd t1, 8(t0)
 li t1, 224
 sd t1, 16(t0)
 li t1, 0
 sd t1, 24(t0)
 li t1, 0
 sd t1, 32(t0)
 li t1, 0
 sd t1, 40(t0)
 li t1, 0
 sd t1, 48(t0)
 li t1, 0
 sd t1, 56(t0)
 li t1, 3
 sd t1, 64(t0)
 li t1, 128
 sd t1, 72(t0)
 li t1, 0
 sd t1, 80(t0)
 li t1, 0
 sd t1, 88(t0)
 li t1, 0
 sd t1, 96(t0)
 li t1, 0
 sd t1, 104(t0)
 li t1, 65536
 sd t1, 112(t0)
 li t1, 274877906944
 sd t1, 120(t0)
 li t0, 263168
 ld t2, 32(t0)
 li t0, 327808
 li t1, 1
 sd t1, 0(t0)
 sd t2, 8(t0)
 li t1, 0
 sd t1, 16(t0)
 sd t1, 24(t0)
 li t0, 263168
 ld t2, 224(t0)
 li t0, 327840
 li t1, 2
 sd t1, 0(t0)
 sd t2, 8(t0)
 li t1, 0
 sd t1, 16(t0)
 sd t1, 24(t0)
 li t0, 263168
 ld t2, 416(t0)
 li t0, 327872
 li t1, 3
 sd t1, 0(t0)
 sd t2, 8(t0)
 li t1, 0
 sd t1, 16(t0)
 sd t1, 24(t0)
 li t0, 262464
 mv t1, s6
 sd t1, 0(t0)
 li t1, 0
 sd t1, 8(t0)
 li t1, 274861129728
 sd t1, 16(t0)
 li t1, 4096
 sd t1, 24(t0)
 li t1, 1
 sd t1, 32(t0)
 li t1, 0
 sd t1, 40(t0)
 li s9, 128
 li a0, 0
 li a1, 0
 li a2, 0
 li a3, 0
 li a4, 0
 li a5, 0
 li a6, 0
 mv a0, s0
 li a1, 262464
 li a7, SYS_MAP
 ecall
 beqz a0, 99f
 j spawn_bad
99:
 beqz t0, 99f
 j spawn_bad
99:
 beqz a1, 99f
 j spawn_bad
99:
 beqz a2, 99f
 j spawn_bad
99:
 beqz a3, 99f
 j spawn_bad
99:
 beqz a4, 99f
 j spawn_bad
99:
 beqz a5, 99f
 j spawn_bad
99:
 beqz a6, 99f
 j spawn_bad
99:
 beqz a7, 99f
 j spawn_bad
99:
 li t0, 262528
 li t1, 65536
 sd t1, 0(t0)
 li t1, 274877841408
 sd t1, 8(t0)
 li t1, 274861129728
 sd t1, 16(t0)
 li t1, 224
 sd t1, 24(t0)
 li t1, 0
 sd t1, 32(t0)
 li t1, 0
 sd t1, 40(t0)
 li s9, 129
 li a0, 0
 li a1, 0
 li a2, 0
 li a3, 0
 li a4, 0
 li a5, 0
 li a6, 0
 mv a0, s0
 li a1, 262528
 li a2, 1
 li a7, SYS_CONFIGURE
 ecall
 li t1, -18
 beq a0, t1, 99f
 j spawn_bad
99:
 beqz t0, 99f
 j spawn_bad
99:
 beqz a1, 99f
 j spawn_bad
99:
 beqz a2, 99f
 j spawn_bad
99:
 beqz a3, 99f
 j spawn_bad
99:
 beqz a4, 99f
 j spawn_bad
99:
 beqz a5, 99f
 j spawn_bad
99:
 beqz a6, 99f
 j spawn_bad
99:
 beqz a7, 99f
 j spawn_bad
99:
 li s9, 130
 li a0, 0
 li a1, 0
 li a2, 0
 li a3, 0
 li a4, 0
 li a5, 0
 li a6, 0
 mv a0, s0
 li a1, 262528
 li a7, SYS_CONFIGURE
 ecall
 beqz a0, 99f
 j spawn_bad
99:
 beqz t0, 99f
 j spawn_bad
99:
 beqz a1, 99f
 j spawn_bad
99:
 beqz a2, 99f
 j spawn_bad
99:
 beqz a3, 99f
 j spawn_bad
99:
 beqz a4, 99f
 j spawn_bad
99:
 beqz a5, 99f
 j spawn_bad
99:
 beqz a6, 99f
 j spawn_bad
99:
 beqz a7, 99f
 j spawn_bad
99:
 li s9, 131
 li a0, 0
 li a1, 0
 li a2, 0
 li a3, 0
 li a4, 0
 li a5, 0
 li a6, 0
 mv a0, s0
 li a7, SYS_START
 ecall
 li t1, -7
 beq a0, t1, 99f
 j spawn_bad
99:
 beqz t0, 99f
 j spawn_bad
99:
 beqz a1, 99f
 j spawn_bad
99:
 beqz a2, 99f
 j spawn_bad
99:
 beqz a3, 99f
 j spawn_bad
99:
 beqz a4, 99f
 j spawn_bad
99:
 beqz a5, 99f
 j spawn_bad
99:
 beqz a6, 99f
 j spawn_bad
99:
 beqz a7, 99f
 j spawn_bad
99:
 li t0, 262144
 ld t2, 32(t0)
 li s9, 132
 li a0, 0
 li a1, 0
 li a2, 0
 li a3, 0
 li a4, 0
 li a5, 0
 li a6, 0
 mv a0, t2
 li a1, 327680
 li a7, SYS_UNMAP
 ecall
 beqz a0, 99f
 j spawn_bad
99:
 beqz t0, 99f
 j spawn_bad
99:
 beqz a1, 99f
 j spawn_bad
99:
 beqz a2, 99f
 j spawn_bad
99:
 beqz a3, 99f
 j spawn_bad
99:
 beqz a4, 99f
 j spawn_bad
99:
 beqz a5, 99f
 j spawn_bad
99:
 beqz a6, 99f
 j spawn_bad
99:
 beqz a7, 99f
 j spawn_bad
99:
 li s9, 133
 li a0, 0
 li a1, 0
 li a2, 0
 li a3, 0
 li a4, 0
 li a5, 0
 li a6, 0
 mv a0, s0
 li a7, SYS_START
 ecall
 beqz a0, 99f
 j spawn_bad
99:
 beqz t0, 99f
 j spawn_bad
99:
 beqz a1, 99f
 j spawn_bad
99:
 beqz a2, 99f
 j spawn_bad
99:
 beqz a3, 99f
 j spawn_bad
99:
 beqz a4, 99f
 j spawn_bad
99:
 beqz a5, 99f
 j spawn_bad
99:
 beqz a6, 99f
 j spawn_bad
99:
 beqz a7, 99f
 j spawn_bad
99:
 li s9, 134
 li a0, 0
 li a1, 0
 li a2, 0
 li a3, 0
 li a4, 0
 li a5, 0
 li a6, 0
 mv a0, s1
 li a1, 1
 li a7, SYS_WAIT
 ecall
 beqz a0, 99f
 j spawn_bad
99:
 beqz t0, 99f
 j spawn_bad
99:
 beqz a1, 99f
 j spawn_bad
99:
 beqz a2, 99f
 j spawn_bad
99:
 beqz a3, 99f
 j spawn_bad
99:
 beqz a4, 99f
 j spawn_bad
99:
 beqz a5, 99f
 j spawn_bad
99:
 beqz a6, 99f
 j spawn_bad
99:
 beqz a7, 99f
 j spawn_bad
99:
 li s9, 135
 li a0, 0
 li a1, 0
 li a2, 0
 li a3, 0
 li a4, 0
 li a5, 0
 li a6, 0
 li a1, 1
 li a7, SYS_WAIT
 ecall
 beqz a0, 99f
 j spawn_bad
99:
 beqz t0, 99f
 j spawn_bad
99:
 beqz a1, 99f
 j spawn_bad
99:
 beqz a2, 99f
 j spawn_bad
99:
 beqz a3, 99f
 j spawn_bad
99:
 beqz a4, 99f
 j spawn_bad
99:
 beqz a5, 99f
 j spawn_bad
99:
 beqz a6, 99f
 j spawn_bad
99:
 beqz a7, 99f
 j spawn_bad
99:
 li s9, 136
 li a0, 0
 li a1, 0
 li a2, 0
 li a3, 0
 li a4, 0
 li a5, 0
 li a6, 0
 mv a0, s0
 li a7, SYS_DESTROY
 ecall
 li t1, -3
 beq a0, t1, 99f
 j spawn_bad
99:
 beqz t0, 99f
 j spawn_bad
99:
 beqz a1, 99f
 j spawn_bad
99:
 beqz a2, 99f
 j spawn_bad
99:
 beqz a3, 99f
 j spawn_bad
99:
 beqz a4, 99f
 j spawn_bad
99:
 beqz a5, 99f
 j spawn_bad
99:
 beqz a6, 99f
 j spawn_bad
99:
 beqz a7, 99f
 j spawn_bad
99:
 li s9, 137
 li a0, 0
 li a1, 0
 li a2, 0
 li a3, 0
 li a4, 0
 li a5, 0
 li a6, 0
 mv a0, s0
 li a1, 5
 li a3, 263168
 li a4, 1
 li a7, SYS_QUERY
 ecall
 li t1, -3
 beq a0, t1, 99f
 j spawn_bad
99:
 beqz t0, 99f
 j spawn_bad
99:
 beqz a1, 99f
 j spawn_bad
99:
 beqz a2, 99f
 j spawn_bad
99:
 beqz a3, 99f
 j spawn_bad
99:
 beqz a4, 99f
 j spawn_bad
99:
 beqz a5, 99f
 j spawn_bad
99:
 beqz a6, 99f
 j spawn_bad
99:
 beqz a7, 99f
 j spawn_bad
99:
 li s9, 138
 li a0, 0
 li a1, 0
 li a2, 0
 li a3, 0
 li a4, 0
 li a5, 0
 li a6, 0
 mv a0, s4
 li a1, 123
 li a5, 1
 li a7, SYS_CALL
 ecall
 beqz a0, 99f
 j spawn_bad
99:
 beqz t0, 99f
 j spawn_bad
99:
 beqz a1, 99f
 j spawn_bad
99:
 beqz a2, 99f
 j spawn_bad
99:
 beqz a4, 99f
 j spawn_bad
99:
 beqz a5, 99f
 j spawn_bad
99:
 beqz a6, 99f
 j spawn_bad
99:
 beqz a7, 99f
 j spawn_bad
99:
 mv s8, a3
 fmv.x.d t0, f31
 li t1, 4660
 beq t0, t1, 99f
 j spawn_bad
99:
 li s9, 139
 li a0, 0
 li a1, 0
 li a2, 0
 li a3, 0
 li a4, 0
 li a5, 0
 li a6, 0
 li a0, -1
 li a7, SYS_WAIT
 ecall
 beqz a0, 99f
 j spawn_bad
99:
 beqz t0, 99f
 j spawn_bad
99:
 mv t1, s1
 beq a1, t1, 99f
 j spawn_bad
99:
 li t1, 1
 beq a2, t1, 99f
 j spawn_bad
99:
 li t1, 42
 beq a3, t1, 99f
 j spawn_bad
99:
 li t1, 2
 beq a4, t1, 99f
 j spawn_bad
99:
 beqz a5, 99f
 j spawn_bad
99:
 beqz a6, 99f
 j spawn_bad
99:
 beqz a7, 99f
 j spawn_bad
99:
 li s9, 140
 li a0, 0
 li a1, 0
 li a2, 0
 li a3, 0
 li a4, 0
 li a5, 0
 li a6, 0
 li a1, 8
 li a5, 3
 li a7, SYS_RECV
 ecall
 beqz a0, 99f
 j spawn_bad
99:
 beqz t0, 99f
 j spawn_bad
99:
 li t1, 3
 beq a1, t1, 99f
 j spawn_bad
99:
 beqz a2, 99f
 j spawn_bad
99:
 beqz a3, 99f
 j spawn_bad
99:
 li t1, 8
 beq a4, t1, 99f
 j spawn_bad
99:
 beqz a5, 99f
 j spawn_bad
99:
 beqz a6, 99f
 j spawn_bad
99:
 beqz a7, 99f
 j spawn_bad
99:
 li s9, 141
 li a0, 0
 li a1, 0
 li a2, 0
 li a3, 0
 li a4, 0
 li a5, 0
 li a6, 0
 li a1, 8
 li a5, 3
 li a7, SYS_RECV
 ecall
 li t1, -11
 beq a0, t1, 99f
 j spawn_bad
99:
 beqz t0, 99f
 j spawn_bad
99:
 beqz a1, 99f
 j spawn_bad
99:
 beqz a2, 99f
 j spawn_bad
99:
 beqz a3, 99f
 j spawn_bad
99:
 beqz a4, 99f
 j spawn_bad
99:
 beqz a5, 99f
 j spawn_bad
99:
 beqz a6, 99f
 j spawn_bad
99:
 beqz a7, 99f
 j spawn_bad
99:
 li s9, 142
 li a0, 0
 li a1, 0
 li a2, 0
 li a3, 0
 li a4, 0
 li a5, 0
 li a6, 0
 mv a0, s8
 li a7, SYS_RQPOLL
 ecall
 beqz a0, 99f
 j spawn_bad
99:
 beqz t0, 99f
 j spawn_bad
99:
 beqz a1, 99f
 j spawn_bad
99:
 beqz a2, 99f
 j spawn_bad
99:
 beqz a3, 99f
 j spawn_bad
99:
 li t1, 321
 beq a4, t1, 99f
 j spawn_bad
99:
 beqz a5, 99f
 j spawn_bad
99:
 beqz a6, 99f
 j spawn_bad
99:
 beqz a7, 99f
 j spawn_bad
99:
 li s9, 143
 li a0, 0
 li a1, 0
 li a2, 0
 li a3, 0
 li a4, 0
 li a5, 0
 li a6, 0
 mv a0, s8
 li a1, 3
 li a7, SYS_RQACK
 ecall
 beqz a0, 99f
 j spawn_bad
99:
 beqz t0, 99f
 j spawn_bad
99:
 beqz a1, 99f
 j spawn_bad
99:
 beqz a2, 99f
 j spawn_bad
99:
 beqz a3, 99f
 j spawn_bad
99:
 beqz a4, 99f
 j spawn_bad
99:
 beqz a5, 99f
 j spawn_bad
99:
 beqz a6, 99f
 j spawn_bad
99:
 beqz a7, 99f
 j spawn_bad
99:
 li s9, 144
 li a0, 0
 li a1, 0
 li a2, 0
 li a3, 0
 li a4, 0
 li a5, 0
 li a6, 0
 mv a0, s1
 li a1, 1
 li a7, SYS_WAIT
 ecall
 li t1, -22
 beq a0, t1, 99f
 j spawn_bad
99:
 beqz t0, 99f
 j spawn_bad
99:
 beqz a1, 99f
 j spawn_bad
99:
 beqz a2, 99f
 j spawn_bad
99:
 beqz a3, 99f
 j spawn_bad
99:
 beqz a4, 99f
 j spawn_bad
99:
 beqz a5, 99f
 j spawn_bad
99:
 beqz a6, 99f
 j spawn_bad
99:
 beqz a7, 99f
 j spawn_bad
99:
 li s9, 145
 li a0, 0
 li a1, 0
 li a2, 0
 li a3, 0
 li a4, 0
 li a5, 0
 li a6, 0
 li a0, -9223372036854775808
 li a1, 1
 li a7, SYS_WAIT
 ecall
 li t1, -1
 beq a0, t1, 99f
 j spawn_bad
99:
 beqz t0, 99f
 j spawn_bad
99:
 beqz a1, 99f
 j spawn_bad
99:
 beqz a2, 99f
 j spawn_bad
99:
 beqz a3, 99f
 j spawn_bad
99:
 beqz a4, 99f
 j spawn_bad
99:
 beqz a5, 99f
 j spawn_bad
99:
 beqz a6, 99f
 j spawn_bad
99:
 beqz a7, 99f
 j spawn_bad
99:
 li s9, 146
 li a0, 0
 li a1, 0
 li a2, 0
 li a3, 0
 li a4, 0
 li a5, 0
 li a6, 0
 li a0, -1
 li a1, 8
 li a7, SYS_WAIT
 ecall
 li t1, -1
 beq a0, t1, 99f
 j spawn_bad
99:
 beqz t0, 99f
 j spawn_bad
99:
 beqz a1, 99f
 j spawn_bad
99:
 beqz a2, 99f
 j spawn_bad
99:
 beqz a3, 99f
 j spawn_bad
99:
 beqz a4, 99f
 j spawn_bad
99:
 beqz a5, 99f
 j spawn_bad
99:
 beqz a6, 99f
 j spawn_bad
99:
 beqz a7, 99f
 j spawn_bad
99:
 li s9, 147
 li a0, 0
 li a1, 0
 li a2, 0
 li a3, 0
 li a4, 0
 li a5, 0
 li a6, 0
 mv a0, s4
 li a5, 1
 li a7, SYS_CALL
 ecall
 li t1, -14
 beq a0, t1, 99f
 j spawn_bad
99:
 beqz t0, 99f
 j spawn_bad
99:
 beqz a1, 99f
 j spawn_bad
99:
 beqz a2, 99f
 j spawn_bad
99:
 beqz a3, 99f
 j spawn_bad
99:
 beqz a4, 99f
 j spawn_bad
99:
 beqz a5, 99f
 j spawn_bad
99:
 beqz a6, 99f
 j spawn_bad
99:
 beqz a7, 99f
 j spawn_bad
99:
 li s9, 148
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
 beqz a0, 99f
 j spawn_bad
99:
 beqz t0, 99f
 j spawn_bad
99:
 li t1, 110
 beq a1, t1, 99f
 j spawn_bad
99:
 li t1, 120
 beq a2, t1, 99f
 j spawn_bad
99:
 li t1, 130
 beq a3, t1, 99f
 j spawn_bad
99:
 li t1, 140
 beq a4, t1, 99f
 j spawn_bad
99:
 beqz a5, 99f
 j spawn_bad
99:
 beqz a6, 99f
 j spawn_bad
99:
 beqz a7, 99f
 j spawn_bad
99:
 li t0, 262400
 li t1, 64
 sd t1, 0(t0)
 li t1, 128
 sd t1, 8(t0)
 li t1, 4294967304
 sd t1, 16(t0)
 li t1, 515396075630
 sd t1, 24(t0)
 li t1, 601295421570
 sd t1, 32(t0)
 li t1, 0
 sd t1, 40(t0)
 li t0, 262144
 ld t2, 16(t0)
 li s9, 149
 li a0, 0
 li a1, 0
 li a2, 0
 li a3, 0
 li a4, 0
 li a5, 0
 li a6, 0
 mv a0, t2
 li a1, 262400
 li a7, SYS_CREATE
 ecall
 beqz a0, 99f
 j spawn_bad
99:
 beqz t0, 99f
 j spawn_bad
99:
 beqz a4, 99f
 j spawn_bad
99:
 beqz a5, 99f
 j spawn_bad
99:
 beqz a6, 99f
 j spawn_bad
99:
 beqz a7, 99f
 j spawn_bad
99:
 mv s0, a1
 mv s1, a2
 mv s2, a3
 li t0, 262312
 sd s1, 0(t0)
 li t0, 262400
 li t1, 4096
 sd t1, 0(t0)
 li t1, 1
 sd t1, 8(t0)
 li t1, 0
 sd t1, 16(t0)
 li t1, 0
 sd t1, 24(t0)
 li t1, 0
 sd t1, 32(t0)
 li t1, 0
 sd t1, 40(t0)
 li s9, 150
 li a0, 0
 li a1, 0
 li a2, 0
 li a3, 0
 li a4, 0
 li a5, 0
 li a6, 0
 mv a0, s0
 li a1, 262400
 li a7, SYS_ALLOC
 ecall
 beqz a0, 99f
 j spawn_bad
99:
 beqz t0, 99f
 j spawn_bad
99:
 beqz a2, 99f
 j spawn_bad
99:
 beqz a3, 99f
 j spawn_bad
99:
 beqz a4, 99f
 j spawn_bad
99:
 beqz a5, 99f
 j spawn_bad
99:
 beqz a6, 99f
 j spawn_bad
99:
 beqz a7, 99f
 j spawn_bad
99:
 mv s3, a1
 li t0, 262144
 ld t2, 32(t0)
 li t0, 262464
 mv t1, s3
 sd t1, 0(t0)
 li t1, 0
 sd t1, 8(t0)
 li t1, 327680
 sd t1, 16(t0)
 li t1, 4096
 sd t1, 24(t0)
 li t1, 3
 sd t1, 32(t0)
 li t1, 0
 sd t1, 40(t0)
 li s9, 151
 li a0, 0
 li a1, 0
 li a2, 0
 li a3, 0
 li a4, 0
 li a5, 0
 li a6, 0
 mv a0, t2
 li a1, 262464
 li a7, SYS_MAP
 ecall
 beqz a0, 99f
 j spawn_bad
99:
 beqz t0, 99f
 j spawn_bad
99:
 beqz a1, 99f
 j spawn_bad
99:
 beqz a2, 99f
 j spawn_bad
99:
 beqz a3, 99f
 j spawn_bad
99:
 beqz a4, 99f
 j spawn_bad
99:
 beqz a5, 99f
 j spawn_bad
99:
 beqz a6, 99f
 j spawn_bad
99:
 beqz a7, 99f
 j spawn_bad
99:
 li s9, 152
 li a0, 0
 li a1, 0
 li a2, 0
 li a3, 0
 li a4, 0
 li a5, 0
 li a6, 0
 mv a0, s0
 li a7, SYS_DESTROY
 ecall
 beqz a0, 99f
 j spawn_bad
99:
 beqz t0, 99f
 j spawn_bad
99:
 beqz a1, 99f
 j spawn_bad
99:
 beqz a2, 99f
 j spawn_bad
99:
 beqz a3, 99f
 j spawn_bad
99:
 beqz a4, 99f
 j spawn_bad
99:
 beqz a5, 99f
 j spawn_bad
99:
 beqz a6, 99f
 j spawn_bad
99:
 beqz a7, 99f
 j spawn_bad
99:
 li s9, 153
 li a0, 0
 li a1, 0
 li a2, 0
 li a3, 0
 li a4, 0
 li a5, 0
 li a6, 0
 mv a0, s2
 li a1, 3
 li a3, 263168
 li a4, 1
 li a7, SYS_QUERY
 ecall
 beqz a0, 99f
 j spawn_bad
99:
 beqz t0, 99f
 j spawn_bad
99:
 li t1, 1
 beq a1, t1, 99f
 j spawn_bad
99:
 beqz a2, 99f
 j spawn_bad
99:
 beqz a3, 99f
 j spawn_bad
99:
 beqz a4, 99f
 j spawn_bad
99:
 beqz a5, 99f
 j spawn_bad
99:
 beqz a6, 99f
 j spawn_bad
99:
 beqz a7, 99f
 j spawn_bad
99:
 li t0, 263168
 ld t2, 56(t0)
 li t1, 1
 beq t2, t1, 99f
 j spawn_bad
99:
 li t0, 263168
 ld t2, 72(t0)
 li t1, 2
 beq t2, t1, 99f
 j spawn_bad
99:
 li t0, 262144
 ld t2, 32(t0)
 li s9, 154
 li a0, 0
 li a1, 0
 li a2, 0
 li a3, 0
 li a4, 0
 li a5, 0
 li a6, 0
 mv a0, t2
 li a1, 327680
 li a7, SYS_UNMAP
 ecall
 beqz a0, 99f
 j spawn_bad
99:
 beqz t0, 99f
 j spawn_bad
99:
 beqz a1, 99f
 j spawn_bad
99:
 beqz a2, 99f
 j spawn_bad
99:
 beqz a3, 99f
 j spawn_bad
99:
 beqz a4, 99f
 j spawn_bad
99:
 beqz a5, 99f
 j spawn_bad
99:
 beqz a6, 99f
 j spawn_bad
99:
 beqz a7, 99f
 j spawn_bad
99:
 li s9, 155
 li a0, 0
 li a1, 0
 li a2, 0
 li a3, 0
 li a4, 0
 li a5, 0
 li a6, 0
 mv a0, s2
 li a1, 3
 li a3, 263168
 li a4, 1
 li a7, SYS_QUERY
 ecall
 beqz a0, 99f
 j spawn_bad
99:
 beqz t0, 99f
 j spawn_bad
99:
 beqz a1, 99f
 j spawn_bad
99:
 beqz a2, 99f
 j spawn_bad
99:
 beqz a3, 99f
 j spawn_bad
99:
 beqz a4, 99f
 j spawn_bad
99:
 beqz a5, 99f
 j spawn_bad
99:
 beqz a6, 99f
 j spawn_bad
99:
 beqz a7, 99f
 j spawn_bad
99:
 li t0, 262272
 li t1, 2
 sd t1, 0(t0)
 li s8, 42
 j spawn_exit
spawn_victim:
 li t0, 262400
 li t1, 64
 sd t1, 0(t0)
 li t1, 128
 sd t1, 8(t0)
 li t1, 4294967304
 sd t1, 16(t0)
 li t1, 85899345930
 sd t1, 24(t0)
 li t1, 171798691870
 sd t1, 32(t0)
 li t1, 0
 sd t1, 40(t0)
 li t0, 262144
 ld t2, 0(t0)
 li s9, 156
 li a0, 0
 li a1, 0
 li a2, 0
 li a3, 0
 li a4, 0
 li a5, 0
 li a6, 0
 mv a0, t2
 li a1, 262400
 li a7, SYS_CREATE
 ecall
 beqz a0, 99f
 j spawn_bad
99:
 beqz t0, 99f
 j spawn_bad
99:
 beqz a3, 99f
 j spawn_bad
99:
 beqz a4, 99f
 j spawn_bad
99:
 beqz a5, 99f
 j spawn_bad
99:
 beqz a6, 99f
 j spawn_bad
99:
 beqz a7, 99f
 j spawn_bad
99:
 mv s0, a1
 mv s1, a2
 mv s2, a3
 li t0, 262280
 sd s1, 0(t0)
 li t0, 262272
 li t1, 99
 sd t1, 0(t0)
 li s9, 157
 li a0, 0
 li a1, 0
 li a2, 0
 li a3, 0
 li a4, 0
 li a5, 0
 li a6, 0
 mv a0, s0
 li a7, SYS_DESTROY
 ecall
 beqz a0, 99f
 j spawn_bad
99:
 beqz t0, 99f
 j spawn_bad
99:
 beqz a1, 99f
 j spawn_bad
99:
 beqz a2, 99f
 j spawn_bad
99:
 beqz a3, 99f
 j spawn_bad
99:
 beqz a4, 99f
 j spawn_bad
99:
 beqz a5, 99f
 j spawn_bad
99:
 beqz a6, 99f
 j spawn_bad
99:
 beqz a7, 99f
 j spawn_bad
99:
 li s8, 250
 j spawn_exit
spawn_killer:
 li t0, 262144
 ld t2, 56(t0)
 li s9, 158
 li a0, 0
 li a1, 0
 li a2, 0
 li a3, 0
 li a4, 0
 li a5, 0
 li a6, 0
 mv a0, t2
 li a7, SYS_TERMINATE
 ecall
 beqz a0, 99f
 j spawn_bad
99:
 beqz t0, 99f
 j spawn_bad
99:
 beqz a1, 99f
 j spawn_bad
99:
 beqz a2, 99f
 j spawn_bad
99:
 beqz a3, 99f
 j spawn_bad
99:
 beqz a4, 99f
 j spawn_bad
99:
 beqz a5, 99f
 j spawn_bad
99:
 beqz a6, 99f
 j spawn_bad
99:
 beqz a7, 99f
 j spawn_bad
99:
 li s8, 42
 j spawn_exit
spawn_bad:
 addi s8, s9, 100
spawn_exit:
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
 j spawn_bad
.balign 8
spawn_child_begin:
 lla s8, spawn_child_params
 ld s0, 0(s8)
 ld s1, 8(s8)
 ld s4, 16(s8)
 ld s5, 24(s8)
 ld s6, 32(s8)
 ld s7, 40(s8)
 fmv.x.d t0, f31
 beqz t0, 99f
 j spawn_child_bad
99:
 mv s2, a0
 li t0, 200
 add t0, s2, t0
 ld s3, 0(t0)
 li s9, 159
 li a0, 0
 li a1, 0
 li a2, 0
 li a3, 0
 li a4, 0
 li a5, 0
 li a6, 0
 li a7, SYS_GETID
 ecall
 beqz a0, 99f
 j spawn_child_bad
99:
 beqz t0, 99f
 j spawn_child_bad
99:
 mv t1, s0
 beq a1, t1, 99f
 j spawn_child_bad
99:
 li t1, 1
 beq a2, t1, 99f
 j spawn_child_bad
99:
 mv t1, s1
 beq a3, t1, 99f
 j spawn_child_bad
99:
 beqz a4, 99f
 j spawn_child_bad
99:
 beqz a5, 99f
 j spawn_child_bad
99:
 beqz a6, 99f
 j spawn_child_bad
99:
 beqz a7, 99f
 j spawn_child_bad
99:
 li s9, 160
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
 j spawn_child_bad
99:
 beqz t0, 99f
 j spawn_child_bad
99:
 mv t1, s4
 beq a1, t1, 99f
 j spawn_child_bad
99:
 mv t1, s5
 beq a2, t1, 99f
 j spawn_child_bad
99:
 mv t1, s6
 beq a3, t1, 99f
 j spawn_child_bad
99:
 mv t1, s7
 beq a4, t1, 99f
 j spawn_child_bad
99:
 beqz a5, 99f
 j spawn_child_bad
99:
 beqz a6, 99f
 j spawn_child_bad
99:
 beqz a7, 99f
 j spawn_child_bad
99:
 li t0, 274877841152
 li t1, 65
 sd t1, 0(t0)
 li t1, 128
 sd t1, 8(t0)
 li t1, 4294967304
 sd t1, 16(t0)
 li t1, 0
 sd t1, 24(t0)
 li t1, 0
 sd t1, 32(t0)
 li t1, 0
 sd t1, 40(t0)
 slli t2, s5, 32
 or t2, t2, s4
 sd t2, 24(t0)
 slli t2, s7, 32
 or t2, t2, s6
 sd t2, 32(t0)
 li s9, 161
 li a0, 0
 li a1, 0
 li a2, 0
 li a3, 0
 li a4, 0
 li a5, 0
 li a6, 0
 mv a0, s3
 li a1, 274877841152
 li a7, SYS_CREATE
 ecall
 li t1, -5
 beq a0, t1, 99f
 j spawn_child_bad
99:
 beqz t0, 99f
 j spawn_child_bad
99:
 beqz a1, 99f
 j spawn_child_bad
99:
 beqz a2, 99f
 j spawn_child_bad
99:
 beqz a3, 99f
 j spawn_child_bad
99:
 beqz a4, 99f
 j spawn_child_bad
99:
 beqz a5, 99f
 j spawn_child_bad
99:
 beqz a6, 99f
 j spawn_child_bad
99:
 beqz a7, 99f
 j spawn_child_bad
99:
 li t0, 274877841152
 li t1, 32
 sd t1, 0(t0)
 li t1, 64
 sd t1, 8(t0)
 li t1, 4294967300
 sd t1, 16(t0)
 li t1, 0
 sd t1, 24(t0)
 li t1, 0
 sd t1, 32(t0)
 li t1, 0
 sd t1, 40(t0)
 li s9, 162
 li a0, 0
 li a1, 0
 li a2, 0
 li a3, 0
 li a4, 0
 li a5, 0
 li a6, 0
 mv a0, s3
 li a1, 274877841152
 li a7, SYS_CREATE
 ecall
 li t1, -5
 beq a0, t1, 99f
 j spawn_child_bad
99:
 beqz t0, 99f
 j spawn_child_bad
99:
 beqz a1, 99f
 j spawn_child_bad
99:
 beqz a2, 99f
 j spawn_child_bad
99:
 beqz a3, 99f
 j spawn_child_bad
99:
 beqz a4, 99f
 j spawn_child_bad
99:
 beqz a5, 99f
 j spawn_child_bad
99:
 beqz a6, 99f
 j spawn_child_bad
99:
 beqz a7, 99f
 j spawn_child_bad
99:
 li s9, 163
 li a0, 0
 li a1, 0
 li a2, 0
 li a3, 0
 li a4, 0
 li a5, 0
 li a6, 0
 li a1, 5
 li a3, 274877837312
 li a4, 4
 li a7, SYS_QUERY
 ecall
 beqz a0, 99f
 j spawn_child_bad
99:
 beqz t0, 99f
 j spawn_child_bad
99:
 li t1, 4
 beq a1, t1, 99f
 j spawn_child_bad
99:
 beqz a2, 99f
 j spawn_child_bad
99:
 beqz a3, 99f
 j spawn_child_bad
99:
 beqz a4, 99f
 j spawn_child_bad
99:
 beqz a5, 99f
 j spawn_child_bad
99:
 beqz a6, 99f
 j spawn_child_bad
99:
 beqz a7, 99f
 j spawn_child_bad
99:
 li t0, 274877837744
 ld t2, 0(t0)
 li t1, 1
 beq t2, t1, 99f
 j spawn_child_bad
99:
 li s9, 164
 li a0, 0
 li a1, 0
 li a2, 0
 li a3, 0
 li a4, 0
 li a5, 0
 li a6, 0
 li a5, 1
 li a7, SYS_RECV
 ecall
 beqz a0, 99f
 j spawn_child_bad
99:
 li t1, 1
 beq t0, t1, 99f
 j spawn_child_bad
99:
 li t1, 1
 beq a1, t1, 99f
 j spawn_child_bad
99:
 mv t1, s1
 beq a2, t1, 99f
 j spawn_child_bad
99:
 li t1, 123
 beq a4, t1, 99f
 j spawn_child_bad
99:
 beqz a5, 99f
 j spawn_child_bad
99:
 beqz a6, 99f
 j spawn_child_bad
99:
 beqz a7, 99f
 j spawn_child_bad
99:
 mv s8, a3
 li s9, 165
 li a0, 0
 li a1, 0
 li a2, 0
 li a3, 0
 li a4, 0
 li a5, 0
 li a6, 0
 mv a0, s8
 li a7, SYS_DEFER
 ecall
 li t1, -7
 beq a0, t1, 99f
 j spawn_child_bad
99:
 beqz t0, 99f
 j spawn_child_bad
99:
 beqz a1, 99f
 j spawn_child_bad
99:
 beqz a2, 99f
 j spawn_child_bad
99:
 beqz a3, 99f
 j spawn_child_bad
99:
 beqz a4, 99f
 j spawn_child_bad
99:
 beqz a5, 99f
 j spawn_child_bad
99:
 beqz a6, 99f
 j spawn_child_bad
99:
 beqz a7, 99f
 j spawn_child_bad
99:
 li s9, 166
 li a0, 0
 li a1, 0
 li a2, 0
 li a3, 0
 li a4, 0
 li a5, 0
 li a6, 0
 mv a0, s8
 li a1, 321
 li a7, SYS_REPLY
 ecall
 beqz a0, 99f
 j spawn_child_bad
99:
 beqz t0, 99f
 j spawn_child_bad
99:
 beqz a1, 99f
 j spawn_child_bad
99:
 beqz a2, 99f
 j spawn_child_bad
99:
 beqz a3, 99f
 j spawn_child_bad
99:
 beqz a4, 99f
 j spawn_child_bad
99:
 beqz a5, 99f
 j spawn_child_bad
99:
 beqz a6, 99f
 j spawn_child_bad
99:
 beqz a7, 99f
 j spawn_child_bad
99:
 li a0, 42
 li a1, 0
 li a2, 0
 li a3, 0
 li a4, 0
 li a5, 0
 li a6, 0
 li a7, SYS_EXIT
 ecall
spawn_child_bad:
 li a0, 251
 li a1, 0
 li a2, 0
 li a3, 0
 li a4, 0
 li a5, 0
 li a6, 0
 li a7, SYS_EXIT
 ecall
.balign 8
spawn_child_params:
 .quad 0,0,0,0,0,0
spawn_child_end:
spawn_end:
