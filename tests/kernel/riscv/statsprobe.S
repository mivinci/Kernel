#include <abi/syscall.h>
#include <abi/ipc.h>
#include <abi/proc.h>
.option norvc
.section .rodata
.balign 8
.global stats_begin, stats_end, stats_target
stats_begin:
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
 li a1, 1
 li a3, 263168
 li a4, 1
 li a7, SYS_QUERY
 ecall
 bnez a0, stats_bad
 bnez t0, stats_bad
 li t1, 1
 bne a1, t1, stats_bad
 bnez a2, stats_bad
 bnez a3, stats_bad
 bnez a4, stats_bad
 bnez a5, stats_bad
 bnez a6, stats_bad
 bnez a7, stats_bad
 li t0, 263168
 ld t2, 8(t0)
 mv t1, s3
 bne t2, t1, stats_bad
 li t0, 263168
 ld t2, 56(t0)
 li t1, PROCESS_RUNNING
 bne t2, t1, stats_bad
 li t0, 263168
 ld t2, 64(t0)
 bnez t2, stats_bad
 li t0, 263168
 ld t2, 96(t0)
 li t1, 8
 bne t2, t1, stats_bad
 li t0, 263168
 ld t2, 104(t0)
 li t1, 4
 bne t2, t1, stats_bad
 li t0, 263168
 ld t2, 112(t0)
 bnez t2, stats_bad
 li t0, 263168
 ld s5, 72(t0)
 li t1, 0
 bleu s5, t1, stats_bad
 li s9, 2
 li a0, 0
 li a1, 0
 li a2, 0
 li a3, 0
 li a4, 0
 li a5, 0
 li a6, 0
 mv a0, s0
 li a1, 1
 li a3, 263168
 li a4, 1
 li a7, SYS_QUERY
 ecall
 bnez a0, stats_bad
 bnez t0, stats_bad
 li t1, 1
 bne a1, t1, stats_bad
 bnez a2, stats_bad
 bnez a3, stats_bad
 bnez a4, stats_bad
 bnez a5, stats_bad
 bnez a6, stats_bad
 bnez a7, stats_bad
 li t0, 263168
 ld t2, 8(t0)
 mv t1, s2
 bne t2, t1, stats_bad
 li t0, 263168
 ld t2, 16(t0)
 li t1, 1
 bne t2, t1, stats_bad
 li t0, 263168
 ld t2, 32(t0)
 li t1, 1
 bne t2, t1, stats_bad
 li t0, 263168
 ld t2, 40(t0)
 li t1, 1
 bne t2, t1, stats_bad
 li t0, 263168
 ld t2, 48(t0)
 li t1, 1
 bne t2, t1, stats_bad
 li t0, 263168
 ld t2, 56(t0)
 li t1, PROCESS_CONSTRUCT
 bne t2, t1, stats_bad
 li t0, 263168
 ld t2, 64(t0)
 bnez t2, stats_bad
 li t0, 263168
 ld t2, 80(t0)
 mv t1, s4
 bne t2, t1, stats_bad
 li t0, 263168
 ld t2, 88(t0)
 bnez t2, stats_bad
 li t0, 263168
 ld t2, 96(t0)
 li t1, 6
 bne t2, t1, stats_bad
 li t0, 263168
 ld t2, 104(t0)
 li t1, 4
 bne t2, t1, stats_bad
 li t0, 263168
 ld t2, 112(t0)
 bnez t2, stats_bad
 li t0, 263168
 ld t2, 120(t0)
 bnez t2, stats_bad
 li t0, 263168
 ld t2, 128(t0)
 bnez t2, stats_bad
 li t0, 263168
 ld t2, 136(t0)
 bnez t2, stats_bad
 li t0, 263168
 ld t2, 144(t0)
 bnez t2, stats_bad
 li t0, 263168
 ld t2, 152(t0)
 bnez t2, stats_bad
 li t0, 263168
 ld t2, 160(t0)
 bnez t2, stats_bad
 li t0, 263168
 ld t2, 168(t0)
 bnez t2, stats_bad
 li t0, 263168
 ld t2, 176(t0)
 bnez t2, stats_bad
 li t0, 263168
 ld t2, 184(t0)
 bnez t2, stats_bad
 li t0, 263168
 ld t2, 72(t0)
 bnez t2, stats_bad
 li s9, 3
 li a0, 0
 li a1, 0
 li a2, 0
 li a3, 0
 li a4, 0
 li a5, 0
 li a6, 0
 mv a0, s0
 li a1, 1
 li a2, 65536
 li a3, 263168
 li a4, 1
 li a7, SYS_QUERY
 ecall
 li t1, -4
 bne a0, t1, stats_bad
 bnez t0, stats_bad
 bnez a1, stats_bad
 bnez a2, stats_bad
 bnez a3, stats_bad
 bnez a4, stats_bad
 bnez a5, stats_bad
 bnez a6, stats_bad
 bnez a7, stats_bad
 li s9, 4
 li a0, 0
 li a1, 0
 li a2, 0
 li a3, 0
 li a4, 0
 li a5, 0
 li a6, 0
 mv a0, s2
 li a1, 1
 li a3, 263168
 li a4, 1
 li a7, SYS_QUERY
 ecall
 li t1, -3
 bne a0, t1, stats_bad
 bnez t0, stats_bad
 bnez a1, stats_bad
 bnez a2, stats_bad
 bnez a3, stats_bad
 bnez a4, stats_bad
 bnez a5, stats_bad
 bnez a6, stats_bad
 bnez a7, stats_bad
 li s9, 5
 li a0, 0
 li a1, 0
 li a2, 0
 li a3, 0
 li a4, 0
 li a5, 0
 li a6, 0
 mv a0, s1
 li a1, 1
 li a3, 263168
 li a4, 1
 li a7, SYS_QUERY
 ecall
 li t1, -3
 bne a0, t1, stats_bad
 bnez t0, stats_bad
 bnez a1, stats_bad
 bnez a2, stats_bad
 bnez a3, stats_bad
 bnez a4, stats_bad
 bnez a5, stats_bad
 bnez a6, stats_bad
 bnez a7, stats_bad
 li s9, 6
 li a0, 0
 li a1, 0
 li a2, 0
 li a3, 0
 li a4, 0
 li a5, 0
 li a6, 0
 mv a0, s0
 li a1, 1
 li a3, 274861129728
 li a4, 1
 li a7, SYS_QUERY
 ecall
 li t1, -2
 bne a0, t1, stats_bad
 bnez t0, stats_bad
 bnez a1, stats_bad
 bnez a2, stats_bad
 bnez a3, stats_bad
 bnez a4, stats_bad
 bnez a5, stats_bad
 bnez a6, stats_bad
 bnez a7, stats_bad
 li t0, 262272
 li t1, 1
 sd t1, 0(t0)
 li s9, 7
 li a0, 0
 li a1, 0
 li a2, 0
 li a3, 0
 li a4, 0
 li a5, 0
 li a6, 0
 li a7, SYS_YIELD
 ecall
 bnez a0, stats_bad
 bnez t0, stats_bad
 bnez a1, stats_bad
 bnez a2, stats_bad
 bnez a3, stats_bad
 bnez a4, stats_bad
 bnez a5, stats_bad
 bnez a6, stats_bad
 bnez a7, stats_bad
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
 li a3, 263168
 li a4, 1
 li a7, SYS_QUERY
 ecall
 bnez a0, stats_bad
 bnez t0, stats_bad
 li t1, 1
 bne a1, t1, stats_bad
 bnez a2, stats_bad
 bnez a3, stats_bad
 bnez a4, stats_bad
 bnez a5, stats_bad
 bnez a6, stats_bad
 bnez a7, stats_bad
 li t0, 263168
 ld t2, 8(t0)
 mv t1, s2
 bne t2, t1, stats_bad
 li t0, 263168
 ld t2, 16(t0)
 li t1, 1
 bne t2, t1, stats_bad
 li t0, 263168
 ld t2, 32(t0)
 li t1, 1
 bne t2, t1, stats_bad
 li t0, 263168
 ld t2, 40(t0)
 li t1, 1
 bne t2, t1, stats_bad
 li t0, 263168
 ld t2, 48(t0)
 li t1, 1
 bne t2, t1, stats_bad
 li t0, 263168
 ld t2, 56(t0)
 li t1, PROCESS_READY
 bne t2, t1, stats_bad
 li t0, 263168
 ld t2, 64(t0)
 bnez t2, stats_bad
 li t0, 263168
 ld t2, 80(t0)
 mv t1, s4
 bne t2, t1, stats_bad
 li t0, 263168
 ld t2, 88(t0)
 bnez t2, stats_bad
 li t0, 263168
 ld t2, 96(t0)
 li t1, 6
 bne t2, t1, stats_bad
 li t0, 263168
 ld t2, 104(t0)
 li t1, 4
 bne t2, t1, stats_bad
 li t0, 263168
 ld t2, 112(t0)
 bnez t2, stats_bad
 li t0, 263168
 ld t2, 120(t0)
 bnez t2, stats_bad
 li t0, 263168
 ld t2, 128(t0)
 bnez t2, stats_bad
 li t0, 263168
 ld t2, 136(t0)
 bnez t2, stats_bad
 li t0, 263168
 ld t2, 144(t0)
 bnez t2, stats_bad
 li t0, 263168
 ld t2, 152(t0)
 bnez t2, stats_bad
 li t0, 263168
 ld t2, 160(t0)
 bnez t2, stats_bad
 li t0, 263168
 ld t2, 168(t0)
 bnez t2, stats_bad
 li t0, 263168
 ld t2, 176(t0)
 bnez t2, stats_bad
 li t0, 263168
 ld t2, 184(t0)
 bnez t2, stats_bad
 li t0, 263168
 ld t2, 72(t0)
 bnez t2, stats_bad
 li s9, 9
 li a0, 0
 li a1, 0
 li a2, 0
 li a3, 0
 li a4, 0
 li a5, 0
 li a6, 0
 li a7, SYS_YIELD
 ecall
 bnez a0, stats_bad
 bnez t0, stats_bad
 bnez a1, stats_bad
 bnez a2, stats_bad
 bnez a3, stats_bad
 bnez a4, stats_bad
 bnez a5, stats_bad
 bnez a6, stats_bad
 bnez a7, stats_bad
 li s9, 10
 li a0, 0
 li a1, 0
 li a2, 0
 li a3, 0
 li a4, 0
 li a5, 0
 li a6, 0
 mv a0, s0
 li a1, 1
 li a3, 263168
 li a4, 1
 li a7, SYS_QUERY
 ecall
 bnez a0, stats_bad
 bnez t0, stats_bad
 li t1, 1
 bne a1, t1, stats_bad
 bnez a2, stats_bad
 bnez a3, stats_bad
 bnez a4, stats_bad
 bnez a5, stats_bad
 bnez a6, stats_bad
 bnez a7, stats_bad
 li t0, 263168
 ld t2, 8(t0)
 mv t1, s2
 bne t2, t1, stats_bad
 li t0, 263168
 ld t2, 16(t0)
 li t1, 1
 bne t2, t1, stats_bad
 li t0, 263168
 ld t2, 32(t0)
 li t1, 1
 bne t2, t1, stats_bad
 li t0, 263168
 ld t2, 40(t0)
 li t1, 1
 bne t2, t1, stats_bad
 li t0, 263168
 ld t2, 48(t0)
 li t1, 1
 bne t2, t1, stats_bad
 li t0, 263168
 ld t2, 56(t0)
 li t1, PROCESS_BLOCKED
 bne t2, t1, stats_bad
 li t0, 263168
 ld t2, 64(t0)
 bnez t2, stats_bad
 li t0, 263168
 ld t2, 80(t0)
 mv t1, s4
 bne t2, t1, stats_bad
 li t0, 263168
 ld t2, 88(t0)
 bnez t2, stats_bad
 li t0, 263168
 ld t2, 96(t0)
 li t1, 6
 bne t2, t1, stats_bad
 li t0, 263168
 ld t2, 104(t0)
 li t1, 4
 bne t2, t1, stats_bad
 li t0, 263168
 ld t2, 112(t0)
 bnez t2, stats_bad
 li t0, 263168
 ld t2, 120(t0)
 bnez t2, stats_bad
 li t0, 263168
 ld t2, 128(t0)
 bnez t2, stats_bad
 li t0, 263168
 ld t2, 136(t0)
 bnez t2, stats_bad
 li t0, 263168
 ld t2, 144(t0)
 bnez t2, stats_bad
 li t0, 263168
 ld t2, 152(t0)
 bnez t2, stats_bad
 li t0, 263168
 ld t2, 160(t0)
 bnez t2, stats_bad
 li t0, 263168
 ld t2, 168(t0)
 bnez t2, stats_bad
 li t0, 263168
 ld t2, 176(t0)
 bnez t2, stats_bad
 li t0, 263168
 ld t2, 184(t0)
 bnez t2, stats_bad
 li t0, 263168
 ld s6, 72(t0)
 li t1, 0
 bleu s6, t1, stats_bad
 li t0, 100000
stats_busy:
 addi t0, t0, -1
 bnez t0, stats_busy
 li s9, 11
 li a0, 0
 li a1, 0
 li a2, 0
 li a3, 0
 li a4, 0
 li a5, 0
 li a6, 0
 mv a0, s0
 li a1, 1
 li a3, 263168
 li a4, 1
 li a7, SYS_QUERY
 ecall
 bnez a0, stats_bad
 bnez t0, stats_bad
 li t1, 1
 bne a1, t1, stats_bad
 bnez a2, stats_bad
 bnez a3, stats_bad
 bnez a4, stats_bad
 bnez a5, stats_bad
 bnez a6, stats_bad
 bnez a7, stats_bad
 li t0, 263168
 ld t2, 8(t0)
 mv t1, s2
 bne t2, t1, stats_bad
 li t0, 263168
 ld t2, 16(t0)
 li t1, 1
 bne t2, t1, stats_bad
 li t0, 263168
 ld t2, 32(t0)
 li t1, 1
 bne t2, t1, stats_bad
 li t0, 263168
 ld t2, 40(t0)
 li t1, 1
 bne t2, t1, stats_bad
 li t0, 263168
 ld t2, 48(t0)
 li t1, 1
 bne t2, t1, stats_bad
 li t0, 263168
 ld t2, 56(t0)
 li t1, PROCESS_BLOCKED
 bne t2, t1, stats_bad
 li t0, 263168
 ld t2, 64(t0)
 bnez t2, stats_bad
 li t0, 263168
 ld t2, 80(t0)
 mv t1, s4
 bne t2, t1, stats_bad
 li t0, 263168
 ld t2, 88(t0)
 bnez t2, stats_bad
 li t0, 263168
 ld t2, 96(t0)
 li t1, 6
 bne t2, t1, stats_bad
 li t0, 263168
 ld t2, 104(t0)
 li t1, 4
 bne t2, t1, stats_bad
 li t0, 263168
 ld t2, 112(t0)
 bnez t2, stats_bad
 li t0, 263168
 ld t2, 120(t0)
 bnez t2, stats_bad
 li t0, 263168
 ld t2, 128(t0)
 bnez t2, stats_bad
 li t0, 263168
 ld t2, 136(t0)
 bnez t2, stats_bad
 li t0, 263168
 ld t2, 144(t0)
 bnez t2, stats_bad
 li t0, 263168
 ld t2, 152(t0)
 bnez t2, stats_bad
 li t0, 263168
 ld t2, 160(t0)
 bnez t2, stats_bad
 li t0, 263168
 ld t2, 168(t0)
 bnez t2, stats_bad
 li t0, 263168
 ld t2, 176(t0)
 bnez t2, stats_bad
 li t0, 263168
 ld t2, 184(t0)
 bnez t2, stats_bad
 li t0, 263168
 ld t2, 72(t0)
 mv t1, s6
 bne t2, t1, stats_bad
 li s9, 12
 li a0, 0
 li a1, 0
 li a2, 0
 li a3, 0
 li a4, 0
 li a5, 0
 li a6, 0
 li a1, 1
 li a3, 263168
 li a4, 1
 li a7, SYS_QUERY
 ecall
 bnez a0, stats_bad
 bnez t0, stats_bad
 li t1, 1
 bne a1, t1, stats_bad
 bnez a2, stats_bad
 bnez a3, stats_bad
 bnez a4, stats_bad
 bnez a5, stats_bad
 bnez a6, stats_bad
 bnez a7, stats_bad
 li t0, 263168
 ld s7, 72(t0)
 mv t1, s5
 bleu s7, t1, stats_bad
 li s9, 13
 li a0, 0
 li a1, 0
 li a2, 0
 li a3, 0
 li a4, 0
 li a5, 0
 li a6, 0
 mv a0, s1
 li a1, 4294967296
 li a7, SYS_NOTIFY
 ecall
 bnez a0, stats_bad
 bnez t0, stats_bad
 bnez a1, stats_bad
 bnez a2, stats_bad
 bnez a3, stats_bad
 bnez a4, stats_bad
 bnez a5, stats_bad
 bnez a6, stats_bad
 bnez a7, stats_bad
 li s9, 14
 li a0, 0
 li a1, 0
 li a2, 0
 li a3, 0
 li a4, 0
 li a5, 0
 li a6, 0
 mv a0, s0
 li a1, 1
 li a3, 263168
 li a4, 1
 li a7, SYS_QUERY
 ecall
 bnez a0, stats_bad
 bnez t0, stats_bad
 li t1, 1
 bne a1, t1, stats_bad
 bnez a2, stats_bad
 bnez a3, stats_bad
 bnez a4, stats_bad
 bnez a5, stats_bad
 bnez a6, stats_bad
 bnez a7, stats_bad
 li t0, 263168
 ld t2, 8(t0)
 mv t1, s2
 bne t2, t1, stats_bad
 li t0, 263168
 ld t2, 16(t0)
 li t1, 1
 bne t2, t1, stats_bad
 li t0, 263168
 ld t2, 32(t0)
 li t1, 1
 bne t2, t1, stats_bad
 li t0, 263168
 ld t2, 40(t0)
 li t1, 1
 bne t2, t1, stats_bad
 li t0, 263168
 ld t2, 48(t0)
 li t1, 1
 bne t2, t1, stats_bad
 li t0, 263168
 ld t2, 56(t0)
 li t1, PROCESS_READY
 bne t2, t1, stats_bad
 li t0, 263168
 ld t2, 64(t0)
 bnez t2, stats_bad
 li t0, 263168
 ld t2, 80(t0)
 mv t1, s4
 bne t2, t1, stats_bad
 li t0, 263168
 ld t2, 88(t0)
 bnez t2, stats_bad
 li t0, 263168
 ld t2, 96(t0)
 li t1, 6
 bne t2, t1, stats_bad
 li t0, 263168
 ld t2, 104(t0)
 li t1, 4
 bne t2, t1, stats_bad
 li t0, 263168
 ld t2, 112(t0)
 bnez t2, stats_bad
 li t0, 263168
 ld t2, 120(t0)
 bnez t2, stats_bad
 li t0, 263168
 ld t2, 128(t0)
 bnez t2, stats_bad
 li t0, 263168
 ld t2, 136(t0)
 bnez t2, stats_bad
 li t0, 263168
 ld t2, 144(t0)
 bnez t2, stats_bad
 li t0, 263168
 ld t2, 152(t0)
 bnez t2, stats_bad
 li t0, 263168
 ld t2, 160(t0)
 bnez t2, stats_bad
 li t0, 263168
 ld t2, 168(t0)
 bnez t2, stats_bad
 li t0, 263168
 ld t2, 176(t0)
 bnez t2, stats_bad
 li t0, 263168
 ld t2, 184(t0)
 bnez t2, stats_bad
 li t0, 263168
 ld t2, 72(t0)
 mv t1, s6
 bne t2, t1, stats_bad
 li s9, 15
 li a0, 0
 li a1, 0
 li a2, 0
 li a3, 0
 li a4, 0
 li a5, 0
 li a6, 0
 li a7, SYS_YIELD
 ecall
 bnez a0, stats_bad
 bnez t0, stats_bad
 bnez a1, stats_bad
 bnez a2, stats_bad
 bnez a3, stats_bad
 bnez a4, stats_bad
 bnez a5, stats_bad
 bnez a6, stats_bad
 bnez a7, stats_bad
 li s9, 16
 li a0, 0
 li a1, 0
 li a2, 0
 li a3, 0
 li a4, 0
 li a5, 0
 li a6, 0
 mv a0, s0
 li a1, 1
 li a3, 263168
 li a4, 1
 li a7, SYS_QUERY
 ecall
 bnez a0, stats_bad
 bnez t0, stats_bad
 li t1, 1
 bne a1, t1, stats_bad
 bnez a2, stats_bad
 bnez a3, stats_bad
 bnez a4, stats_bad
 bnez a5, stats_bad
 bnez a6, stats_bad
 bnez a7, stats_bad
 li t0, 263168
 ld t2, 8(t0)
 mv t1, s2
 bne t2, t1, stats_bad
 li t0, 263168
 ld t2, 16(t0)
 li t1, 1
 bne t2, t1, stats_bad
 li t0, 263168
 ld t2, 32(t0)
 li t1, 1
 bne t2, t1, stats_bad
 li t0, 263168
 ld t2, 40(t0)
 li t1, 1
 bne t2, t1, stats_bad
 li t0, 263168
 ld t2, 48(t0)
 li t1, 1
 bne t2, t1, stats_bad
 li t0, 263168
 ld t2, 56(t0)
 li t1, PROCESS_EXITING
 bne t2, t1, stats_bad
 li t0, 263168
 ld t2, 64(t0)
 bnez t2, stats_bad
 li t0, 263168
 ld t2, 80(t0)
 mv t1, s4
 bne t2, t1, stats_bad
 li t0, 263168
 ld t2, 88(t0)
 bnez t2, stats_bad
 li t0, 263168
 ld t2, 96(t0)
 li t1, 6
 bne t2, t1, stats_bad
 li t0, 263168
 ld t2, 104(t0)
 li t1, 4
 bne t2, t1, stats_bad
 li t0, 263168
 ld t2, 112(t0)
 bnez t2, stats_bad
 li t0, 263168
 ld t2, 120(t0)
 bnez t2, stats_bad
 li t0, 263168
 ld t2, 128(t0)
 bnez t2, stats_bad
 li t0, 263168
 ld t2, 136(t0)
 bnez t2, stats_bad
 li t0, 263168
 ld t2, 144(t0)
 bnez t2, stats_bad
 li t0, 263168
 ld t2, 152(t0)
 bnez t2, stats_bad
 li t0, 263168
 ld t2, 160(t0)
 bnez t2, stats_bad
 li t0, 263168
 ld t2, 168(t0)
 bnez t2, stats_bad
 li t0, 263168
 ld t2, 176(t0)
 bnez t2, stats_bad
 li t0, 263168
 ld t2, 184(t0)
 bnez t2, stats_bad
 li t0, 263168
 ld s7, 72(t0)
 mv t1, s6
 bleu s7, t1, stats_bad
 li t0, 262272
 li t1, 2
 sd t1, 0(t0)
 li s9, 17
 li a0, 0
 li a1, 0
 li a2, 0
 li a3, 0
 li a4, 0
 li a5, 0
 li a6, 0
 li a7, SYS_YIELD
 ecall
 bnez a0, stats_bad
 bnez t0, stats_bad
 bnez a1, stats_bad
 bnez a2, stats_bad
 bnez a3, stats_bad
 bnez a4, stats_bad
 bnez a5, stats_bad
 bnez a6, stats_bad
 bnez a7, stats_bad
 li s9, 18
 li a0, 0
 li a1, 0
 li a2, 0
 li a3, 0
 li a4, 0
 li a5, 0
 li a6, 0
 mv a0, s0
 li a1, 1
 li a3, 263168
 li a4, 1
 li a7, SYS_QUERY
 ecall
 bnez a0, stats_bad
 bnez t0, stats_bad
 bnez a1, stats_bad
 bnez a2, stats_bad
 bnez a3, stats_bad
 bnez a4, stats_bad
 bnez a5, stats_bad
 bnez a6, stats_bad
 bnez a7, stats_bad
 li s9, 19
 li a0, 0
 li a1, 0
 li a2, 0
 li a3, 0
 li a4, 0
 li a5, 0
 li a6, 0
 mv a0, s0
 li a1, 4
 li a3, 263168
 li a4, 1
 li a7, SYS_QUERY
 ecall
 bnez a0, stats_bad
 bnez t0, stats_bad
 li t1, 1
 bne a1, t1, stats_bad
 bnez a2, stats_bad
 bnez a3, stats_bad
 bnez a4, stats_bad
 bnez a5, stats_bad
 bnez a6, stats_bad
 bnez a7, stats_bad
 li t0, 263168
 ld t2, 8(t0)
 mv t1, s2
 bne t2, t1, stats_bad
 li t0, 263168
 ld t2, 40(t0)
 li t1, 1
 bne t2, t1, stats_bad
 li t0, 263168
 ld t2, 48(t0)
 li t1, 42
 bne t2, t1, stats_bad
 li t0, 262144
 ld s5, 40(t0)
 li t0, 262144
 ld s6, 48(t0)
 li t0, 262144
 ld s7, 56(t0)
 li s9, 20
 li a0, 0
 li a1, 0
 li a2, 0
 li a3, 0
 li a4, 0
 li a5, 0
 li a6, 0
 mv a0, s5
 li a1, 1
 li a3, 263168
 li a4, 1
 li a7, SYS_QUERY
 ecall
 bnez a0, stats_bad
 bnez t0, stats_bad
 li t1, 1
 bne a1, t1, stats_bad
 bnez a2, stats_bad
 bnez a3, stats_bad
 bnez a4, stats_bad
 bnez a5, stats_bad
 bnez a6, stats_bad
 bnez a7, stats_bad
 li t0, 263168
 ld t2, 8(t0)
 mv t1, s6
 bne t2, t1, stats_bad
 li t0, 263168
 ld t2, 16(t0)
 li t1, 1
 bne t2, t1, stats_bad
 li t0, 263168
 ld t2, 56(t0)
 li t1, PROCESS_CONSTRUCT
 bne t2, t1, stats_bad
 li t0, 263168
 ld t2, 64(t0)
 bnez t2, stats_bad
 li t0, 263168
 ld t2, 72(t0)
 bnez t2, stats_bad
 li t0, 263168
 ld t2, 80(t0)
 mv t1, s7
 bne t2, t1, stats_bad
 li t0, 263168
 ld t2, 88(t0)
 bnez t2, stats_bad
 li t0, 263168
 ld t2, 96(t0)
 li t1, 2
 bne t2, t1, stats_bad
 li t0, 263168
 ld t2, 104(t0)
 bnez t2, stats_bad
 li t0, 263168
 ld t2, 112(t0)
 bnez t2, stats_bad
 li s8, 42
 j stats_exit
stats_target:
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
 li s9, 21
 li a0, 0
 li a1, 0
 li a2, 0
 li a3, 0
 li a4, 0
 li a5, 0
 li a6, 0
 li a1, 1
 li a3, 263168
 li a4, 1
 li a7, SYS_QUERY
 ecall
 bnez a0, stats_bad
 bnez t0, stats_bad
 li t1, 1
 bne a1, t1, stats_bad
 bnez a2, stats_bad
 bnez a3, stats_bad
 bnez a4, stats_bad
 bnez a5, stats_bad
 bnez a6, stats_bad
 bnez a7, stats_bad
 li t0, 263168
 ld t2, 8(t0)
 mv t1, s2
 bne t2, t1, stats_bad
 li t0, 263168
 ld t2, 56(t0)
 li t1, PROCESS_RUNNING
 bne t2, t1, stats_bad
 li t0, 263168
 ld s5, 72(t0)
 li t1, 0
 bleu s5, t1, stats_bad
 li s9, 22
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
 bnez a0, stats_bad
 bnez t0, stats_bad
 li t1, 3
 bne a1, t1, stats_bad
 bnez a2, stats_bad
 bnez a3, stats_bad
 li t1, 4294967296
 bne a4, t1, stats_bad
 bnez a5, stats_bad
 bnez a6, stats_bad
 bnez a7, stats_bad
 li s9, 23
 li a0, 0
 li a1, 0
 li a2, 0
 li a3, 0
 li a4, 0
 li a5, 0
 li a6, 0
 li a1, 1
 li a3, 263168
 li a4, 1
 li a7, SYS_QUERY
 ecall
 bnez a0, stats_bad
 bnez t0, stats_bad
 li t1, 1
 bne a1, t1, stats_bad
 bnez a2, stats_bad
 bnez a3, stats_bad
 bnez a4, stats_bad
 bnez a5, stats_bad
 bnez a6, stats_bad
 bnez a7, stats_bad
 li t0, 263168
 ld s6, 72(t0)
 mv t1, s5
 bleu s6, t1, stats_bad
 li s8, 42
 j stats_exit
stats_bad:
 addi s8, s9, 100
stats_exit:
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
 j stats_bad
stats_end:
