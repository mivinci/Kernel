#include <abi/syscall.h>
.option norvc
#include <abi/ipc.h>
.section .rodata
.balign 8
.global ipc_begin, ipc_end, ipc_server
ipc_begin:
 li t0, 262144
 ld s0, 0(t0)
 ld s1, 8(t0)
 ld s2, 16(t0)
 ld s3, 24(t0)
 ld s4, 32(t0)
 ld s8, 48(t0)
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
 li a1, 2
 li a3, 263168
 li a4, 1
 li a7, SYS_QUERY
 ecall
 bnez a0, ipc_bad
 bnez t0, ipc_bad
 bnez a1, ipc_bad
 bnez a2, ipc_bad
 bnez a3, ipc_bad
 bnez a4, ipc_bad
 bnez a5, ipc_bad
 bnez a6, ipc_bad
 bnez a7, ipc_bad
 li s9, 2
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
 li t1, -11
 bne a0, t1, ipc_bad
 bnez t0, ipc_bad
 bnez a1, ipc_bad
 bnez a2, ipc_bad
 bnez a3, ipc_bad
 bnez a4, ipc_bad
 bnez a5, ipc_bad
 bnez a6, ipc_bad
 bnez a7, ipc_bad
 li s9, 3
 li a0, 0
 li a1, 0
 li a2, 0
 li a3, 0
 li a4, 0
 li a5, 0
 li a6, 0
 li a5, 2
 li a7, SYS_RECV
 ecall
 li t1, -1
 bne a0, t1, ipc_bad
 bnez t0, ipc_bad
 bnez a1, ipc_bad
 bnez a2, ipc_bad
 bnez a3, ipc_bad
 bnez a4, ipc_bad
 bnez a5, ipc_bad
 bnez a6, ipc_bad
 bnez a7, ipc_bad
 li s9, 4
 li a0, 0
 li a1, 0
 li a2, 0
 li a3, 0
 li a4, 0
 li a5, 0
 li a6, 0
 li a1, 1
 li a5, 1
 li a7, SYS_RECV
 ecall
 li t1, -11
 bne a0, t1, ipc_bad
 bnez t0, ipc_bad
 bnez a1, ipc_bad
 bnez a2, ipc_bad
 bnez a3, ipc_bad
 bnez a4, ipc_bad
 bnez a5, ipc_bad
 bnez a6, ipc_bad
 bnez a7, ipc_bad
 li s9, 5
 li a0, 0
 li a1, 0
 li a2, 0
 li a3, 0
 li a4, 0
 li a5, 0
 li a6, 0
 mv a0, s2
 li a1, 1
 li a2, 2
 li a3, 3
 li a4, 4
 li a5, 8
 li a7, SYS_SEND
 ecall
 li t1, -1
 bne a0, t1, ipc_bad
 bnez t0, ipc_bad
 bnez a1, ipc_bad
 bnez a2, ipc_bad
 bnez a3, ipc_bad
 bnez a4, ipc_bad
 bnez a5, ipc_bad
 bnez a6, ipc_bad
 bnez a7, ipc_bad
 li s9, 6
 li a0, 0
 li a1, 0
 li a2, 0
 li a3, 0
 li a4, 0
 li a5, 0
 li a6, 0
 mv a0, s2
 li a1, 1
 li a2, 2
 li a3, 3
 li a4, 4
 li a7, SYS_CALL
 ecall
 li t1, -3
 bne a0, t1, ipc_bad
 bnez t0, ipc_bad
 bnez a1, ipc_bad
 bnez a2, ipc_bad
 bnez a3, ipc_bad
 bnez a4, ipc_bad
 bnez a5, ipc_bad
 bnez a6, ipc_bad
 bnez a7, ipc_bad
 li s9, 7
 li a0, 0
 li a1, 0
 li a2, 0
 li a3, 0
 li a4, 0
 li a5, 0
 li a6, 0
 mv a0, s0
 li a1, 1
 li a2, 2
 li a3, 3
 li a4, 4
 li a5, 16
 li a7, SYS_CALL
 ecall
 li t1, -1
 bne a0, t1, ipc_bad
 bnez t0, ipc_bad
 bnez a1, ipc_bad
 bnez a2, ipc_bad
 bnez a3, ipc_bad
 bnez a4, ipc_bad
 bnez a5, ipc_bad
 bnez a6, ipc_bad
 bnez a7, ipc_bad
 li s9, 8
 li a0, 0
 li a1, 0
 li a2, 0
 li a3, 0
 li a4, 0
 li a5, 0
 li a6, 0
 mv a0, s0
 li a1, 1
 li a2, 2
 li a3, 3
 li a4, 4
 li a5, 4294967296
 li a7, SYS_CALL
 ecall
 li t1, -1
 bne a0, t1, ipc_bad
 bnez t0, ipc_bad
 bnez a1, ipc_bad
 bnez a2, ipc_bad
 bnez a3, ipc_bad
 bnez a4, ipc_bad
 bnez a5, ipc_bad
 bnez a6, ipc_bad
 bnez a7, ipc_bad
 li s9, 9
 li a0, 0
 li a1, 0
 li a2, 0
 li a3, 0
 li a4, 0
 li a5, 0
 li a6, 0
 mv a0, s0
 li a1, 1
 li a2, 2
 li a3, 3
 li a4, 4
 li a5, 2
 li a7, SYS_CALL
 ecall
 li t1, -18
 bne a0, t1, ipc_bad
 bnez t0, ipc_bad
 bnez a1, ipc_bad
 bnez a2, ipc_bad
 bnez a3, ipc_bad
 bnez a4, ipc_bad
 bnez a5, ipc_bad
 bnez a6, ipc_bad
 bnez a7, ipc_bad
 li s9, 10
 li a0, 0
 li a1, 0
 li a2, 0
 li a3, 0
 li a4, 0
 li a5, 0
 li a6, 0
 mv a0, s1
 li a1, 1
 li a2, 2
 li a3, 3
 li a4, 4
 li a7, SYS_CALL
 ecall
 li t1, -16
 bne a0, t1, ipc_bad
 bnez t0, ipc_bad
 bnez a1, ipc_bad
 bnez a2, ipc_bad
 bnez a3, ipc_bad
 bnez a4, ipc_bad
 bnez a5, ipc_bad
 bnez a6, ipc_bad
 bnez a7, ipc_bad
 li s9, 11
 li a0, 0
 li a1, 0
 li a2, 0
 li a3, 0
 li a4, 0
 li a5, 0
 li a6, 0
 mv a0, s1
 li a1, 1
 li a2, 2
 li a3, 3
 li a4, 4
 li a5, 1
 li a7, SYS_CALL
 ecall
 bnez a0, ipc_bad
 bnez t0, ipc_bad
 bnez a1, ipc_bad
 bnez a2, ipc_bad
 bnez a4, ipc_bad
 bnez a5, ipc_bad
 bnez a6, ipc_bad
 bnez a7, ipc_bad
 mv s5, a3
 li s9, 12
 li a0, 0
 li a1, 0
 li a2, 0
 li a3, 0
 li a4, 0
 li a5, 0
 li a6, 0
 li a1, 2
 li a3, 263168
 li a4, 1
 li a7, SYS_QUERY
 ecall
 bnez a0, ipc_bad
 bnez t0, ipc_bad
 li t1, 1
 bne a1, t1, ipc_bad
 bnez a2, ipc_bad
 bnez a3, ipc_bad
 bnez a4, ipc_bad
 bnez a5, ipc_bad
 bnez a6, ipc_bad
 bnez a7, ipc_bad
 li t0, 263168
 ld a7, 0(t0)
 li t1, 2
 bne a7, t1, ipc_bad
 li t0, 263168
 ld a7, 8(t0)
 mv t1, s4
 bne a7, t1, ipc_bad
 li t0, 263168
 ld a7, 16(t0)
 li t1, 1
 bne a7, t1, ipc_bad
 li t0, 263168
 ld a7, 32(t0)
 mv t1, s4
 bne a7, t1, ipc_bad
 li t0, 263168
 ld a7, 40(t0)
 li t1, 1
 bne a7, t1, ipc_bad
 li t0, 263168
 ld a7, 48(t0)
 mv t1, s4
 bne a7, t1, ipc_bad
 li t0, 263168
 ld a7, 56(t0)
 li t1, 1
 bne a7, t1, ipc_bad
 li t0, 263168
 ld a7, 64(t0)
 li t1, 1
 bne a7, t1, ipc_bad
 li t0, 263168
 ld a7, 72(t0)
 bnez a7, ipc_bad
 li t0, 263168
 ld a7, 80(t0)
 bnez a7, ipc_bad
 li t0, 263168
 ld a7, 88(t0)
 bnez a7, ipc_bad
 li t0, 263168
 ld a7, 96(t0)
 bnez a7, ipc_bad
 li t0, 263168
 ld a7, 104(t0)
 bnez a7, ipc_bad
 li t0, 263168
 ld a7, 112(t0)
 bnez a7, ipc_bad
 li t0, 263168
 ld a7, 120(t0)
 bnez a7, ipc_bad
 li t0, 263168
 ld a7, 128(t0)
 bnez a7, ipc_bad
 li t0, 263168
 ld a7, 136(t0)
 bnez a7, ipc_bad
 li t0, 263168
 ld a7, 144(t0)
 bnez a7, ipc_bad
 li t0, 263168
 ld a7, 152(t0)
 bnez a7, ipc_bad
 li t0, 263168
 ld a7, 160(t0)
 bnez a7, ipc_bad
 li t0, 263168
 ld a7, 168(t0)
 bnez a7, ipc_bad
 li t0, 263168
 ld a7, 176(t0)
 bnez a7, ipc_bad
 li t0, 263168
 ld a7, 184(t0)
 bnez a7, ipc_bad
 li s9, 13
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
 li a4, 1
 li a7, SYS_QUERY
 ecall
 bnez a0, ipc_bad
 bnez t0, ipc_bad
 bnez a1, ipc_bad
 bnez a2, ipc_bad
 bnez a3, ipc_bad
 bnez a4, ipc_bad
 bnez a5, ipc_bad
 bnez a6, ipc_bad
 bnez a7, ipc_bad
 li t0, 576460752303423487
 and s7, s5, t0
 li s9, 14
 li a0, 0
 li a1, 0
 li a2, 0
 li a3, 0
 li a4, 0
 li a5, 0
 li a6, 0
 mv a0, s8
 li a1, 2
 mv a2, s7
 li a3, 263168
 li a4, 1
 li a7, SYS_QUERY
 ecall
 li t1, -4
 bne a0, t1, ipc_bad
 bnez t0, ipc_bad
 bnez a1, ipc_bad
 bnez a2, ipc_bad
 bnez a3, ipc_bad
 bnez a4, ipc_bad
 bnez a5, ipc_bad
 bnez a6, ipc_bad
 bnez a7, ipc_bad
 li s9, 15
 li a0, 0
 li a1, 0
 li a2, 0
 li a3, 0
 li a4, 0
 li a5, 0
 li a6, 0
 mv a0, s5
 li a7, SYS_RQWAIT
 ecall
 li t1, -16
 bne a0, t1, ipc_bad
 bnez t0, ipc_bad
 bnez a1, ipc_bad
 bnez a2, ipc_bad
 bnez a3, ipc_bad
 bnez a4, ipc_bad
 bnez a5, ipc_bad
 bnez a6, ipc_bad
 bnez a7, ipc_bad
 li s9, 16
 li a0, 0
 li a1, 0
 li a2, 0
 li a3, 0
 li a4, 0
 li a5, 0
 li a6, 0
 mv a0, s5
 li a7, SYS_RQCANCEL
 ecall
 bnez a0, ipc_bad
 bnez t0, ipc_bad
 bnez a1, ipc_bad
 bnez a2, ipc_bad
 bnez a3, ipc_bad
 bnez a4, ipc_bad
 bnez a5, ipc_bad
 bnez a6, ipc_bad
 bnez a7, ipc_bad
 li s9, 17
 li a0, 0
 li a1, 0
 li a2, 0
 li a3, 0
 li a4, 0
 li a5, 0
 li a6, 0
 li a1, 2
 li a3, 263168
 li a4, 1
 li a7, SYS_QUERY
 ecall
 bnez a0, ipc_bad
 bnez t0, ipc_bad
 li t1, 1
 bne a1, t1, ipc_bad
 bnez a2, ipc_bad
 bnez a3, ipc_bad
 bnez a4, ipc_bad
 bnez a5, ipc_bad
 bnez a6, ipc_bad
 bnez a7, ipc_bad
 li t0, 263168
 ld a7, 64(t0)
 li t1, 3
 bne a7, t1, ipc_bad
 li t0, 263168
 ld a7, 72(t0)
 li t1, -13
 bne a7, t1, ipc_bad
 li t0, 263168
 ld a7, 80(t0)
 li t1, 2
 bne a7, t1, ipc_bad
 li s9, 18
 li a0, 0
 li a1, 0
 li a2, 0
 li a3, 0
 li a4, 0
 li a5, 0
 li a6, 0
 mv a0, s5
 li a7, SYS_RQPOLL
 ecall
 li t1, -13
 bne a0, t1, ipc_bad
 bnez t0, ipc_bad
 bnez a1, ipc_bad
 bnez a2, ipc_bad
 bnez a3, ipc_bad
 bnez a4, ipc_bad
 bnez a5, ipc_bad
 bnez a6, ipc_bad
 bnez a7, ipc_bad
 li s9, 19
 li a0, 0
 li a1, 0
 li a2, 0
 li a3, 0
 li a4, 0
 li a5, 0
 li a6, 0
 mv a0, s5
 li a1, 3
 li a7, SYS_RQACK
 ecall
 bnez a0, ipc_bad
 bnez t0, ipc_bad
 bnez a1, ipc_bad
 bnez a2, ipc_bad
 bnez a3, ipc_bad
 bnez a4, ipc_bad
 bnez a5, ipc_bad
 bnez a6, ipc_bad
 bnez a7, ipc_bad
 li s9, 20
 li a0, 0
 li a1, 0
 li a2, 0
 li a3, 0
 li a4, 0
 li a5, 0
 li a6, 0
 li a1, 2
 li a3, 263168
 li a4, 1
 li a7, SYS_QUERY
 ecall
 bnez a0, ipc_bad
 bnez t0, ipc_bad
 bnez a1, ipc_bad
 bnez a2, ipc_bad
 bnez a3, ipc_bad
 bnez a4, ipc_bad
 bnez a5, ipc_bad
 bnez a6, ipc_bad
 bnez a7, ipc_bad
 li s9, 21
 li a0, 0
 li a1, 0
 li a2, 0
 li a3, 0
 li a4, 0
 li a5, 0
 li a6, 0
 li a1, 2
 li a5, 3
 li a7, SYS_RECV
 ecall
 bnez a0, ipc_bad
 bnez t0, ipc_bad
 li t1, 3
 bne a1, t1, ipc_bad
 bnez a2, ipc_bad
 bnez a3, ipc_bad
 li t1, 2
 bne a4, t1, ipc_bad
 bnez a5, ipc_bad
 bnez a6, ipc_bad
 bnez a7, ipc_bad
 li s9, 22
 li a0, 0
 li a1, 0
 li a2, 0
 li a3, 0
 li a4, 0
 li a5, 0
 li a6, 0
 mv a0, s0
 li a1, 10
 li a2, 2
 li a3, 3
 li a4, 4
 li a7, SYS_CALL
 ecall
 bnez a0, ipc_bad
 bnez t0, ipc_bad
 bnez a1, ipc_bad
 bnez a2, ipc_bad
 bnez a3, ipc_bad
 li t1, 20
 bne a4, t1, ipc_bad
 li t1, 21
 bne a5, t1, ipc_bad
 li t1, 22
 bne a6, t1, ipc_bad
 li t1, 23
 bne a7, t1, ipc_bad
 fmv.x.d t0, f31
 li t1, 4660
 bne t0, t1, ipc_bad
 li s9, 23
 li a0, 0
 li a1, 0
 li a2, 0
 li a3, 0
 li a4, 0
 li a5, 0
 li a6, 0
 li a1, 2
 li a5, 3
 li a7, SYS_RECV
 ecall
 bnez a0, ipc_bad
 bnez t0, ipc_bad
 li t1, 3
 bne a1, t1, ipc_bad
 bnez a2, ipc_bad
 bnez a3, ipc_bad
 li t1, 2
 bne a4, t1, ipc_bad
 bnez a5, ipc_bad
 bnez a6, ipc_bad
 bnez a7, ipc_bad
 li s9, 24
 li a0, 0
 li a1, 0
 li a2, 0
 li a3, 0
 li a4, 0
 li a5, 0
 li a6, 0
 mv a0, s0
 li a1, 30
 li a2, 2
 li a3, 3
 li a4, 4
 li a5, 9
 li a7, SYS_CALL
 ecall
 bnez a0, ipc_bad
 bnez t0, ipc_bad
 bnez a1, ipc_bad
 bnez a2, ipc_bad
 bnez a4, ipc_bad
 bnez a5, ipc_bad
 bnez a6, ipc_bad
 bnez a7, ipc_bad
 mv s5, a3
 li s9, 25
 li a0, 0
 li a1, 0
 li a2, 0
 li a3, 0
 li a4, 0
 li a5, 0
 li a6, 0
 li a7, SYS_YIELD
 ecall
 bnez a0, ipc_bad
 bnez t0, ipc_bad
 bnez a1, ipc_bad
 bnez a2, ipc_bad
 bnez a3, ipc_bad
 bnez a4, ipc_bad
 bnez a5, ipc_bad
 bnez a6, ipc_bad
 bnez a7, ipc_bad
 li s9, 26
 li a0, 0
 li a1, 0
 li a2, 0
 li a3, 0
 li a4, 0
 li a5, 0
 li a6, 0
 li a1, 2
 li a3, 263168
 li a4, 1
 li a7, SYS_QUERY
 ecall
 bnez a0, ipc_bad
 bnez t0, ipc_bad
 li t1, 1
 bne a1, t1, ipc_bad
 bnez a2, ipc_bad
 bnez a3, ipc_bad
 bnez a4, ipc_bad
 bnez a5, ipc_bad
 bnez a6, ipc_bad
 bnez a7, ipc_bad
 li t0, 263168
 ld a7, 64(t0)
 li t1, 2
 bne a7, t1, ipc_bad
 li t0, 263168
 ld a7, 80(t0)
 bnez a7, ipc_bad
 li s9, 27
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
 li a4, 1
 li a7, SYS_QUERY
 ecall
 bnez a0, ipc_bad
 bnez t0, ipc_bad
 li t1, 1
 bne a1, t1, ipc_bad
 bnez a2, ipc_bad
 bnez a3, ipc_bad
 bnez a4, ipc_bad
 bnez a5, ipc_bad
 bnez a6, ipc_bad
 bnez a7, ipc_bad
 li t0, 263168
 ld a7, 8(t0)
 mv t1, s3
 bne a7, t1, ipc_bad
 li t0, 263168
 ld a7, 64(t0)
 li t1, 2
 bne a7, t1, ipc_bad
 li t0, 263168
 ld a7, 80(t0)
 bnez a7, ipc_bad
 li s9, 28
 li a0, 0
 li a1, 0
 li a2, 0
 li a3, 0
 li a4, 0
 li a5, 0
 li a6, 0
 mv a0, s5
 li a7, SYS_RQCANCEL
 ecall
 bnez a0, ipc_bad
 bnez t0, ipc_bad
 bnez a1, ipc_bad
 bnez a2, ipc_bad
 bnez a3, ipc_bad
 bnez a4, ipc_bad
 bnez a5, ipc_bad
 bnez a6, ipc_bad
 bnez a7, ipc_bad
 li s9, 29
 li a0, 0
 li a1, 0
 li a2, 0
 li a3, 0
 li a4, 0
 li a5, 0
 li a6, 0
 li a1, 2
 li a3, 263168
 li a4, 1
 li a7, SYS_QUERY
 ecall
 bnez a0, ipc_bad
 bnez t0, ipc_bad
 li t1, 1
 bne a1, t1, ipc_bad
 bnez a2, ipc_bad
 bnez a3, ipc_bad
 bnez a4, ipc_bad
 bnez a5, ipc_bad
 bnez a6, ipc_bad
 bnez a7, ipc_bad
 li t0, 263168
 ld a7, 64(t0)
 li t1, 2
 bne a7, t1, ipc_bad
 li t0, 263168
 ld a7, 80(t0)
 li t1, 8
 bne a7, t1, ipc_bad
 li s9, 30
 li a0, 0
 li a1, 0
 li a2, 0
 li a3, 0
 li a4, 0
 li a5, 0
 li a6, 0
 mv a0, s5
 li a7, SYS_RQWAIT
 ecall
 bnez a0, ipc_bad
 bnez t0, ipc_bad
 bnez a1, ipc_bad
 bnez a2, ipc_bad
 bnez a3, ipc_bad
 li t1, 31
 bne a4, t1, ipc_bad
 li t1, 32
 bne a5, t1, ipc_bad
 li t1, 33
 bne a6, t1, ipc_bad
 li t1, 34
 bne a7, t1, ipc_bad
 fmv.x.d t0, f31
 li t1, 4660
 bne t0, t1, ipc_bad
 li s9, 31
 li a0, 0
 li a1, 0
 li a2, 0
 li a3, 0
 li a4, 0
 li a5, 0
 li a6, 0
 li a1, 2
 li a5, 3
 li a7, SYS_RECV
 ecall
 bnez a0, ipc_bad
 bnez t0, ipc_bad
 li t1, 3
 bne a1, t1, ipc_bad
 bnez a2, ipc_bad
 bnez a3, ipc_bad
 li t1, 2
 bne a4, t1, ipc_bad
 bnez a5, ipc_bad
 bnez a6, ipc_bad
 bnez a7, ipc_bad
 li s9, 32
 li a0, 0
 li a1, 0
 li a2, 0
 li a3, 0
 li a4, 0
 li a5, 0
 li a6, 0
 mv a0, s5
 li a7, SYS_RQPOLL
 ecall
 bnez a0, ipc_bad
 bnez t0, ipc_bad
 bnez a1, ipc_bad
 bnez a2, ipc_bad
 bnez a3, ipc_bad
 li t1, 31
 bne a4, t1, ipc_bad
 li t1, 32
 bne a5, t1, ipc_bad
 li t1, 33
 bne a6, t1, ipc_bad
 li t1, 34
 bne a7, t1, ipc_bad
 li s9, 33
 li a0, 0
 li a1, 0
 li a2, 0
 li a3, 0
 li a4, 0
 li a5, 0
 li a6, 0
 mv a0, s5
 li a1, 4294967296
 li a7, SYS_RQACK
 ecall
 li t1, -1
 bne a0, t1, ipc_bad
 bnez t0, ipc_bad
 bnez a1, ipc_bad
 bnez a2, ipc_bad
 bnez a3, ipc_bad
 bnez a4, ipc_bad
 bnez a5, ipc_bad
 bnez a6, ipc_bad
 bnez a7, ipc_bad
 li s9, 34
 li a0, 0
 li a1, 0
 li a2, 0
 li a3, 0
 li a4, 0
 li a5, 0
 li a6, 0
 mv a0, s5
 li a7, SYS_RQPOLL
 ecall
 bnez a0, ipc_bad
 bnez t0, ipc_bad
 bnez a1, ipc_bad
 bnez a2, ipc_bad
 bnez a3, ipc_bad
 li t1, 31
 bne a4, t1, ipc_bad
 li t1, 32
 bne a5, t1, ipc_bad
 li t1, 33
 bne a6, t1, ipc_bad
 li t1, 34
 bne a7, t1, ipc_bad
 li s9, 35
 li a0, 0
 li a1, 0
 li a2, 0
 li a3, 0
 li a4, 0
 li a5, 0
 li a6, 0
 mv a0, s5
 li a1, 1
 li a7, SYS_RQACK
 ecall
 bnez a0, ipc_bad
 bnez t0, ipc_bad
 bnez a1, ipc_bad
 bnez a2, ipc_bad
 bnez a3, ipc_bad
 bnez a4, ipc_bad
 bnez a5, ipc_bad
 bnez a6, ipc_bad
 bnez a7, ipc_bad
 li s9, 36
 li a0, 0
 li a1, 0
 li a2, 0
 li a3, 0
 li a4, 0
 li a5, 0
 li a6, 0
 li a1, 2
 li a3, 263168
 li a4, 1
 li a7, SYS_QUERY
 ecall
 bnez a0, ipc_bad
 bnez t0, ipc_bad
 li t1, 1
 bne a1, t1, ipc_bad
 bnez a2, ipc_bad
 bnez a3, ipc_bad
 bnez a4, ipc_bad
 bnez a5, ipc_bad
 bnez a6, ipc_bad
 bnez a7, ipc_bad
 li t0, 263168
 ld a7, 64(t0)
 li t1, 3
 bne a7, t1, ipc_bad
 li t0, 263168
 ld a7, 80(t0)
 li t1, 11
 bne a7, t1, ipc_bad
 li s9, 37
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
 li a4, 1
 li a7, SYS_QUERY
 ecall
 bnez a0, ipc_bad
 bnez t0, ipc_bad
 bnez a1, ipc_bad
 bnez a2, ipc_bad
 bnez a3, ipc_bad
 bnez a4, ipc_bad
 bnez a5, ipc_bad
 bnez a6, ipc_bad
 bnez a7, ipc_bad
 li s9, 38
 li a0, 0
 li a1, 0
 li a2, 0
 li a3, 0
 li a4, 0
 li a5, 0
 li a6, 0
 mv a0, s5
 li a7, SYS_RQPOLL
 ecall
 li t1, -6
 bne a0, t1, ipc_bad
 bnez t0, ipc_bad
 bnez a1, ipc_bad
 bnez a2, ipc_bad
 bnez a3, ipc_bad
 bnez a4, ipc_bad
 bnez a5, ipc_bad
 bnez a6, ipc_bad
 bnez a7, ipc_bad
 li s9, 39
 li a0, 0
 li a1, 0
 li a2, 0
 li a3, 0
 li a4, 0
 li a5, 0
 li a6, 0
 mv a0, s5
 li a1, 1
 li a7, SYS_RQPOLL
 ecall
 bnez a0, ipc_bad
 bnez t0, ipc_bad
 bnez a1, ipc_bad
 bnez a2, ipc_bad
 bnez a3, ipc_bad
 bnez a4, ipc_bad
 bnez a5, ipc_bad
 bnez a6, ipc_bad
 bnez a7, ipc_bad
 li s9, 40
 li a0, 0
 li a1, 0
 li a2, 0
 li a3, 0
 li a4, 0
 li a5, 0
 li a6, 0
 mv a0, s5
 li a1, 2
 li a7, SYS_RQACK
 ecall
 bnez a0, ipc_bad
 bnez t0, ipc_bad
 bnez a1, ipc_bad
 bnez a2, ipc_bad
 bnez a3, ipc_bad
 bnez a4, ipc_bad
 bnez a5, ipc_bad
 bnez a6, ipc_bad
 bnez a7, ipc_bad
 li s9, 41
 li a0, 0
 li a1, 0
 li a2, 0
 li a3, 0
 li a4, 0
 li a5, 0
 li a6, 0
 li a1, 2
 li a3, 263168
 li a4, 1
 li a7, SYS_QUERY
 ecall
 bnez a0, ipc_bad
 bnez t0, ipc_bad
 bnez a1, ipc_bad
 bnez a2, ipc_bad
 bnez a3, ipc_bad
 bnez a4, ipc_bad
 bnez a5, ipc_bad
 bnez a6, ipc_bad
 bnez a7, ipc_bad
 li s9, 42
 li a0, 0
 li a1, 0
 li a2, 0
 li a3, 0
 li a4, 0
 li a5, 0
 li a6, 0
 mv a0, s5
 li a7, SYS_RQPOLL
 ecall
 li t1, -4
 bne a0, t1, ipc_bad
 bnez t0, ipc_bad
 bnez a1, ipc_bad
 bnez a2, ipc_bad
 bnez a3, ipc_bad
 bnez a4, ipc_bad
 bnez a5, ipc_bad
 bnez a6, ipc_bad
 bnez a7, ipc_bad
 li s9, 43
 li a0, 0
 li a1, 0
 li a2, 0
 li a3, 0
 li a4, 0
 li a5, 0
 li a6, 0
 mv a0, s2
 li a1, 40
 li a2, 2
 li a3, 3
 li a4, 4
 li a7, SYS_SEND
 ecall
 bnez a0, ipc_bad
 bnez t0, ipc_bad
 bnez a1, ipc_bad
 bnez a2, ipc_bad
 bnez a3, ipc_bad
 bnez a4, ipc_bad
 bnez a5, ipc_bad
 bnez a6, ipc_bad
 bnez a7, ipc_bad
 fmv.x.d t0, f31
 li t1, 4660
 bne t0, t1, ipc_bad
 li s9, 44
 li a0, 0
 li a1, 0
 li a2, 0
 li a3, 0
 li a4, 0
 li a5, 0
 li a6, 0
 mv a0, s0
 li a1, 50
 li a2, 2
 li a3, 3
 li a4, 4
 li a7, SYS_CALL
 ecall
 bnez a0, ipc_bad
 bnez t0, ipc_bad
 bnez a1, ipc_bad
 bnez a2, ipc_bad
 bnez a3, ipc_bad
 li t1, 51
 bne a4, t1, ipc_bad
 li t1, 52
 bne a5, t1, ipc_bad
 li t1, 53
 bne a6, t1, ipc_bad
 li t1, 54
 bne a7, t1, ipc_bad
 li s9, 45
 li a0, 0
 li a1, 0
 li a2, 0
 li a3, 0
 li a4, 0
 li a5, 0
 li a6, 0
 mv a0, s0
 li a1, 60
 li a2, 2
 li a3, 3
 li a4, 4
 li a7, SYS_CALL
 ecall
 li t1, -14
 bne a0, t1, ipc_bad
 bnez t0, ipc_bad
 bnez a1, ipc_bad
 bnez a2, ipc_bad
 bnez a4, ipc_bad
 bnez a5, ipc_bad
 bnez a6, ipc_bad
 bnez a7, ipc_bad
 mv s5, a3
 li t0, 262144
 sd s5, 40(t0)
 li s9, 46
 li a0, 0
 li a1, 0
 li a2, 0
 li a3, 0
 li a4, 0
 li a5, 0
 li a6, 0
 li a1, 2
 li a3, 263168
 li a4, 1
 li a7, SYS_QUERY
 ecall
 bnez a0, ipc_bad
 bnez t0, ipc_bad
 li t1, 1
 bne a1, t1, ipc_bad
 bnez a2, ipc_bad
 bnez a3, ipc_bad
 bnez a4, ipc_bad
 bnez a5, ipc_bad
 bnez a6, ipc_bad
 bnez a7, ipc_bad
 li t0, 263168
 ld a7, 64(t0)
 li t1, 3
 bne a7, t1, ipc_bad
 li t0, 263168
 ld a7, 72(t0)
 li t1, -14
 bne a7, t1, ipc_bad
 li t0, 263168
 ld a7, 80(t0)
 bnez a7, ipc_bad
 li s9, 47
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
 li a4, 1
 li a7, SYS_QUERY
 ecall
 bnez a0, ipc_bad
 bnez t0, ipc_bad
 li t1, 1
 bne a1, t1, ipc_bad
 bnez a2, ipc_bad
 bnez a3, ipc_bad
 bnez a4, ipc_bad
 bnez a5, ipc_bad
 bnez a6, ipc_bad
 bnez a7, ipc_bad
 li t0, 263168
 ld a7, 8(t0)
 mv t1, s3
 bne a7, t1, ipc_bad
 li t0, 263168
 ld a7, 32(t0)
 mv t1, s4
 bne a7, t1, ipc_bad
 li t0, 263168
 ld a7, 48(t0)
 mv t1, s3
 bne a7, t1, ipc_bad
 li t0, 263168
 ld a7, 64(t0)
 li t1, 3
 bne a7, t1, ipc_bad
 li t0, 263168
 ld a7, 80(t0)
 bnez a7, ipc_bad
 li s9, 48
 li a0, 0
 li a1, 0
 li a2, 0
 li a3, 0
 li a4, 0
 li a5, 0
 li a6, 0
 mv a0, s5
 li a7, SYS_RQPOLL
 ecall
 li t1, -14
 bne a0, t1, ipc_bad
 bnez t0, ipc_bad
 bnez a1, ipc_bad
 bnez a2, ipc_bad
 bnez a3, ipc_bad
 bnez a4, ipc_bad
 bnez a5, ipc_bad
 bnez a6, ipc_bad
 bnez a7, ipc_bad
 li s9, 49
 li a0, 0
 li a1, 0
 li a2, 0
 li a3, 0
 li a4, 0
 li a5, 0
 li a6, 0
 li a1, 2
 li a5, 3
 li a7, SYS_RECV
 ecall
 bnez a0, ipc_bad
 bnez t0, ipc_bad
 li t1, 3
 bne a1, t1, ipc_bad
 bnez a2, ipc_bad
 bnez a3, ipc_bad
 li t1, 2
 bne a4, t1, ipc_bad
 bnez a5, ipc_bad
 bnez a6, ipc_bad
 bnez a7, ipc_bad
 li s9, 50
 li a0, 0
 li a1, 0
 li a2, 0
 li a3, 0
 li a4, 0
 li a5, 0
 li a6, 0
 mv a0, s5
 li a1, 1
 li a7, SYS_RQACK
 ecall
 bnez a0, ipc_bad
 bnez t0, ipc_bad
 bnez a1, ipc_bad
 bnez a2, ipc_bad
 bnez a3, ipc_bad
 bnez a4, ipc_bad
 bnez a5, ipc_bad
 bnez a6, ipc_bad
 bnez a7, ipc_bad
 li s9, 51
 li a0, 0
 li a1, 0
 li a2, 0
 li a3, 0
 li a4, 0
 li a5, 0
 li a6, 0
 mv a0, s5
 li a1, 1
 li a7, SYS_RQWAIT
 ecall
 bnez a0, ipc_bad
 bnez t0, ipc_bad
 bnez a1, ipc_bad
 bnez a2, ipc_bad
 bnez a3, ipc_bad
 bnez a4, ipc_bad
 bnez a5, ipc_bad
 bnez a6, ipc_bad
 bnez a7, ipc_bad
 li s9, 52
 li a0, 0
 li a1, 0
 li a2, 0
 li a3, 0
 li a4, 0
 li a5, 0
 li a6, 0
 li a1, 2
 li a3, 263168
 li a4, 1
 li a7, SYS_QUERY
 ecall
 bnez a0, ipc_bad
 bnez t0, ipc_bad
 li t1, 1
 bne a1, t1, ipc_bad
 bnez a2, ipc_bad
 bnez a3, ipc_bad
 bnez a4, ipc_bad
 bnez a5, ipc_bad
 bnez a6, ipc_bad
 bnez a7, ipc_bad
 li t0, 263168
 ld a7, 64(t0)
 li t1, 3
 bne a7, t1, ipc_bad
 li t0, 263168
 ld a7, 72(t0)
 li t1, -14
 bne a7, t1, ipc_bad
 li t0, 263168
 ld a7, 80(t0)
 li t1, 3
 bne a7, t1, ipc_bad
 li s9, 53
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
 li a4, 1
 li a7, SYS_QUERY
 ecall
 bnez a0, ipc_bad
 bnez t0, ipc_bad
 bnez a1, ipc_bad
 bnez a2, ipc_bad
 bnez a3, ipc_bad
 bnez a4, ipc_bad
 bnez a5, ipc_bad
 bnez a6, ipc_bad
 bnez a7, ipc_bad
 li s9, 54
 li a0, 0
 li a1, 0
 li a2, 0
 li a3, 0
 li a4, 0
 li a5, 0
 li a6, 0
 li a1, 2
 li a5, 3
 li a7, SYS_RECV
 ecall
 bnez a0, ipc_bad
 bnez t0, ipc_bad
 li t1, 3
 bne a1, t1, ipc_bad
 bnez a2, ipc_bad
 bnez a3, ipc_bad
 li t1, 2
 bne a4, t1, ipc_bad
 bnez a5, ipc_bad
 bnez a6, ipc_bad
 bnez a7, ipc_bad
 li s9, 55
 li a0, 0
 li a1, 0
 li a2, 0
 li a3, 0
 li a4, 0
 li a5, 0
 li a6, 0
 mv a0, s5
 li a1, 2
 li a7, SYS_RQACK
 ecall
 bnez a0, ipc_bad
 bnez t0, ipc_bad
 bnez a1, ipc_bad
 bnez a2, ipc_bad
 bnez a3, ipc_bad
 bnez a4, ipc_bad
 bnez a5, ipc_bad
 bnez a6, ipc_bad
 bnez a7, ipc_bad
 li s9, 56
 li a0, 0
 li a1, 0
 li a2, 0
 li a3, 0
 li a4, 0
 li a5, 0
 li a6, 0
 mv a0, s0
 li a1, 61
 li a2, 2
 li a3, 3
 li a4, 4
 li a7, SYS_CALL
 ecall
 li t1, -14
 bne a0, t1, ipc_bad
 bnez t0, ipc_bad
 bnez a1, ipc_bad
 bnez a2, ipc_bad
 bnez a3, ipc_bad
 bnez a4, ipc_bad
 bnez a5, ipc_bad
 bnez a6, ipc_bad
 bnez a7, ipc_bad
 li s9, 57
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
 bnez a0, ipc_bad
 bnez t0, ipc_bad
 bnez a1, ipc_bad
 bnez a2, ipc_bad
 bnez a3, ipc_bad
 bnez a4, ipc_bad
 bnez a5, ipc_bad
 bnez a6, ipc_bad
 bnez a7, ipc_bad
 j ipc_success
ipc_server:
 li t0, 262144
 ld s0, 0(t0)
 ld s1, 8(t0)
 ld s2, 16(t0)
 ld s3, 24(t0)
 ld s4, 32(t0)
 ld s8, 48(t0)
 li t0, 4660
 fmv.d.x f31, t0
 li s9, 58
 li a0, 0
 li a1, 0
 li a2, 0
 li a3, 0
 li a4, 0
 li a5, 0
 li a6, 0
 li a7, SYS_RECV
 ecall
 bnez a0, ipc_bad
 li t1, 1
 bne t0, t1, ipc_bad
 li t1, 1
 bne a1, t1, ipc_bad
 mv t1, s3
 bne a2, t1, ipc_bad
 li t1, 10
 bne a4, t1, ipc_bad
 li t1, 2
 bne a5, t1, ipc_bad
 li t1, 3
 bne a6, t1, ipc_bad
 li t1, 4
 bne a7, t1, ipc_bad
 mv s6, a3
 li s9, 59
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
 li a4, 1
 li a7, SYS_QUERY
 ecall
 bnez a0, ipc_bad
 bnez t0, ipc_bad
 li t1, 1
 bne a1, t1, ipc_bad
 bnez a2, ipc_bad
 bnez a3, ipc_bad
 bnez a4, ipc_bad
 bnez a5, ipc_bad
 bnez a6, ipc_bad
 bnez a7, ipc_bad
 li t0, 263168
 ld a7, 8(t0)
 mv t1, s3
 bne a7, t1, ipc_bad
 li t0, 263168
 ld a7, 56(t0)
 li t1, 4
 bne a7, t1, ipc_bad
 li t0, 263168
 ld a7, 64(t0)
 mv t1, s4
 bne a7, t1, ipc_bad
 li t0, 263168
 ld a7, 112(t0)
 li t1, 1
 bne a7, t1, ipc_bad
 li s9, 60
 li a0, 0
 li a1, 0
 li a2, 0
 li a3, 0
 li a4, 0
 li a5, 0
 li a6, 0
 li a1, 2
 li a3, 263168
 li a4, 1
 li a7, SYS_QUERY
 ecall
 bnez a0, ipc_bad
 bnez t0, ipc_bad
 li t1, 1
 bne a1, t1, ipc_bad
 bnez a2, ipc_bad
 bnez a3, ipc_bad
 bnez a4, ipc_bad
 bnez a5, ipc_bad
 bnez a6, ipc_bad
 bnez a7, ipc_bad
 li t0, 263168
 ld a7, 8(t0)
 mv t1, s4
 bne a7, t1, ipc_bad
 li t0, 263168
 ld a7, 32(t0)
 mv t1, s3
 bne a7, t1, ipc_bad
 li t0, 263168
 ld a7, 48(t0)
 mv t1, s4
 bne a7, t1, ipc_bad
 li t0, 263168
 ld a7, 64(t0)
 li t1, 2
 bne a7, t1, ipc_bad
 li t0, 263168
 ld a7, 80(t0)
 bnez a7, ipc_bad
#if BOOT_CASE == 29
 li s9, 61
 li a0, 0
 li a1, 0
 li a2, 0
 li a3, 0
 li a4, 0
 li a5, 0
 li a6, 0
 mv a0, s0
 li a1, 70
 li a2, 2
 li a3, 3
 li a4, 4
 li a7, SYS_CALL
 ecall
 li t1, -17
 bne a0, t1, ipc_bad
 bnez t0, ipc_bad
 bnez a1, ipc_bad
 bnez a2, ipc_bad
 bnez a3, ipc_bad
 bnez a4, ipc_bad
 bnez a5, ipc_bad
 bnez a6, ipc_bad
 bnez a7, ipc_bad
#else
 li s9, 62
 li a0, 0
 li a1, 0
 li a2, 0
 li a3, 0
 li a4, 0
 li a5, 0
 li a6, 0
 mv a0, s0
 li a1, 70
 li a2, 2
 li a3, 3
 li a4, 4
 li a7, SYS_CALL
 ecall
 li t1, -16
 bne a0, t1, ipc_bad
 bnez t0, ipc_bad
 bnez a1, ipc_bad
 bnez a2, ipc_bad
 bnez a3, ipc_bad
 bnez a4, ipc_bad
 bnez a5, ipc_bad
 bnez a6, ipc_bad
 bnez a7, ipc_bad
#endif
 li s9, 63
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
 li t1, -7
 bne a0, t1, ipc_bad
 bnez t0, ipc_bad
 bnez a1, ipc_bad
 bnez a2, ipc_bad
 bnez a3, ipc_bad
 bnez a4, ipc_bad
 bnez a5, ipc_bad
 bnez a6, ipc_bad
 bnez a7, ipc_bad
 li s9, 64
 li a0, 0
 li a1, 0
 li a2, 0
 li a3, 0
 li a4, 0
 li a5, 0
 li a6, 0
 mv a0, s6
 li a1, 20
 li a2, 21
 li a3, 22
 li a4, 23
 li a5, 4
 li a7, SYS_REPLY_RECV
 ecall
 li t1, -1
 bne a0, t1, ipc_bad
 bnez t0, ipc_bad
 bnez a1, ipc_bad
 bnez a2, ipc_bad
 bnez a3, ipc_bad
 bnez a4, ipc_bad
 bnez a5, ipc_bad
 bnez a6, ipc_bad
 bnez a7, ipc_bad
 li s9, 65
 li a0, 0
 li a1, 0
 li a2, 0
 li a3, 0
 li a4, 0
 li a5, 0
 li a6, 0
 mv a0, s6
 li a7, SYS_RQCHECK
 ecall
 bnez a0, ipc_bad
 bnez t0, ipc_bad
 li t1, 1
 bne a1, t1, ipc_bad
 bnez a2, ipc_bad
 bnez a3, ipc_bad
 bnez a4, ipc_bad
 bnez a5, ipc_bad
 bnez a6, ipc_bad
 bnez a7, ipc_bad
 li s9, 66
 li a0, 0
 li a1, 0
 li a2, 0
 li a3, 0
 li a4, 0
 li a5, 0
 li a6, 0
 mv a0, s6
 li a1, 20
 li a2, 21
 li a3, 22
 li a4, 23
 li a5, 1
 li a7, SYS_REPLY_RECV
 ecall
 li t1, -11
 bne a0, t1, ipc_bad
 bnez t0, ipc_bad
 bnez a1, ipc_bad
 bnez a2, ipc_bad
 bnez a3, ipc_bad
 bnez a4, ipc_bad
 bnez a5, ipc_bad
 bnez a6, ipc_bad
 bnez a7, ipc_bad
 li s9, 67
 li a0, 0
 li a1, 0
 li a2, 0
 li a3, 0
 li a4, 0
 li a5, 0
 li a6, 0
 li a7, SYS_RECV
 ecall
 bnez a0, ipc_bad
 li t1, 1
 bne t0, t1, ipc_bad
 li t1, 1
 bne a1, t1, ipc_bad
 mv t1, s3
 bne a2, t1, ipc_bad
 li t1, 30
 bne a4, t1, ipc_bad
 li t1, 2
 bne a5, t1, ipc_bad
 li t1, 3
 bne a6, t1, ipc_bad
 li t1, 4
 bne a7, t1, ipc_bad
 mv s6, a3
 li s9, 68
 li a0, 0
 li a1, 0
 li a2, 0
 li a3, 0
 li a4, 0
 li a5, 0
 li a6, 0
 mv a0, s6
 li a7, SYS_DEFER
 ecall
 bnez a0, ipc_bad
 bnez t0, ipc_bad
 bnez a1, ipc_bad
 bnez a2, ipc_bad
 bnez a3, ipc_bad
 bnez a4, ipc_bad
 bnez a5, ipc_bad
 bnez a6, ipc_bad
 bnez a7, ipc_bad
 li s9, 69
 li a0, 0
 li a1, 0
 li a2, 0
 li a3, 0
 li a4, 0
 li a5, 0
 li a6, 0
 li a1, 4
 li a5, 2
 li a7, SYS_RECV
 ecall
 bnez a0, ipc_bad
 bnez t0, ipc_bad
 li t1, 3
 bne a1, t1, ipc_bad
 bnez a2, ipc_bad
 bnez a3, ipc_bad
 li t1, 4
 bne a4, t1, ipc_bad
 bnez a5, ipc_bad
 bnez a6, ipc_bad
 bnez a7, ipc_bad
 li s9, 70
 li a0, 0
 li a1, 0
 li a2, 0
 li a3, 0
 li a4, 0
 li a5, 0
 li a6, 0
 mv a0, s6
 li a7, SYS_RQCHECK
 ecall
 bnez a0, ipc_bad
 bnez t0, ipc_bad
 li t1, 3
 bne a1, t1, ipc_bad
 bnez a2, ipc_bad
 bnez a3, ipc_bad
 bnez a4, ipc_bad
 bnez a5, ipc_bad
 bnez a6, ipc_bad
 bnez a7, ipc_bad
 li s9, 71
 li a0, 0
 li a1, 0
 li a2, 0
 li a3, 0
 li a4, 0
 li a5, 0
 li a6, 0
 mv a0, s6
 li a7, SYS_RQFINISH
 ecall
 li t1, -6
 bne a0, t1, ipc_bad
 bnez t0, ipc_bad
 bnez a1, ipc_bad
 bnez a2, ipc_bad
 bnez a3, ipc_bad
 bnez a4, ipc_bad
 bnez a5, ipc_bad
 bnez a6, ipc_bad
 bnez a7, ipc_bad
 li s9, 72
 li a0, 0
 li a1, 0
 li a2, 0
 li a3, 0
 li a4, 0
 li a5, 0
 li a6, 0
 mv a0, s6
 li a1, 31
 li a2, 32
 li a3, 33
 li a4, 34
 li a7, SYS_REPLY
 ecall
 bnez a0, ipc_bad
 bnez t0, ipc_bad
 bnez a1, ipc_bad
 bnez a2, ipc_bad
 bnez a3, ipc_bad
 bnez a4, ipc_bad
 bnez a5, ipc_bad
 bnez a6, ipc_bad
 bnez a7, ipc_bad
 li s9, 73
 li a0, 0
 li a1, 0
 li a2, 0
 li a3, 0
 li a4, 0
 li a5, 0
 li a6, 0
 li a7, SYS_RECV
 ecall
 bnez a0, ipc_bad
 li t1, 1
 bne t0, t1, ipc_bad
 li t1, 2
 bne a1, t1, ipc_bad
 mv t1, s3
 bne a2, t1, ipc_bad
 bnez a3, ipc_bad
 li t1, 40
 bne a4, t1, ipc_bad
 li t1, 2
 bne a5, t1, ipc_bad
 li t1, 3
 bne a6, t1, ipc_bad
 li t1, 4
 bne a7, t1, ipc_bad
 li s9, 74
 li a0, 0
 li a1, 0
 li a2, 0
 li a3, 0
 li a4, 0
 li a5, 0
 li a6, 0
 li a7, SYS_RECV
 ecall
 bnez a0, ipc_bad
 li t1, 1
 bne t0, t1, ipc_bad
 li t1, 1
 bne a1, t1, ipc_bad
 mv t1, s3
 bne a2, t1, ipc_bad
 li t1, 50
 bne a4, t1, ipc_bad
 li t1, 2
 bne a5, t1, ipc_bad
 li t1, 3
 bne a6, t1, ipc_bad
 li t1, 4
 bne a7, t1, ipc_bad
 mv s6, a3
 li s9, 75
 li a0, 0
 li a1, 0
 li a2, 0
 li a3, 0
 li a4, 0
 li a5, 0
 li a6, 0
 mv a0, s6
 li a1, 51
 li a2, 52
 li a3, 53
 li a4, 54
 li a7, SYS_REPLY
 ecall
 bnez a0, ipc_bad
 bnez t0, ipc_bad
 bnez a1, ipc_bad
 bnez a2, ipc_bad
 bnez a3, ipc_bad
 bnez a4, ipc_bad
 bnez a5, ipc_bad
 bnez a6, ipc_bad
 bnez a7, ipc_bad
 fmv.x.d t0, f31
 li t1, 4660
 bne t0, t1, ipc_bad
 li s9, 76
 li a0, 0
 li a1, 0
 li a2, 0
 li a3, 0
 li a4, 0
 li a5, 0
 li a6, 0
 li a7, SYS_RECV
 ecall
 bnez a0, ipc_bad
 li t1, 1
 bne t0, t1, ipc_bad
 li t1, 1
 bne a1, t1, ipc_bad
 mv t1, s3
 bne a2, t1, ipc_bad
 li t1, 60
 bne a4, t1, ipc_bad
 li t1, 2
 bne a5, t1, ipc_bad
 li t1, 3
 bne a6, t1, ipc_bad
 li t1, 4
 bne a7, t1, ipc_bad
 mv s6, a3
ipc_success:
 li s7, 42
 j ipc_exit
ipc_bad:
 addi s7, s9, 100
ipc_exit:
 li a0, 0
 li a1, 0
 li a2, 0
 li a3, 0
 li a4, 0
 li a5, 0
 li a6, 0
 mv a0, s7
 li a7, SYS_EXIT
 ecall
 j ipc_bad
ipc_end:
