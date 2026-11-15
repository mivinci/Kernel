#include <abi/syscall.h>
.section .rodata
.balign 8
.global native_begin, native_end
.option norvc
.global native_fault
native_begin:
 li s8, 0
 j native_common
native_fault:
 li s8, 1
native_common:
 mv s5, a0
 ld t0, 72(s5)
 add t0, t0, s5
 ld s0, 8(t0)
 ld s1, 40(t0)
 auipc s11, 0
 fmv.d.x f31, s11
 li t0, 33
 fscsr t0
 addi sp, sp, -128
 li t6, 30
 li a0, 1
 li a1, 0
 li a2, 0
 li a3, 0
 li a4, 0
 li a5, 0
 li a6, 0
 li a7, SYS_CLOCK_GETTIME
 ecall
 bnez t0, native_bad
 bnez a0, native_bad
 bltz a1, native_bad
 li t0, 1000000000
 bgeu a2, t0, native_bad
 bnez a3, native_bad
 bnez a4, native_bad
 bnez a5, native_bad
 bnez a6, native_bad
 bnez a7, native_bad
 li t6, 30
 li a0, 2
 li a1, 0
 li a2, 0
 li a3, 0
 li a4, 0
 li a5, 0
 li a6, 0
 li a7, SYS_CLOCK_GETTIME
 ecall
 bnez t0, native_bad
 bnez a0, native_bad
 bltz a1, native_bad
 li t0, 1000000000
 bgeu a2, t0, native_bad
 bnez a3, native_bad
 bnez a4, native_bad
 bnez a5, native_bad
 bnez a6, native_bad
 bnez a7, native_bad
 li t6, 30
 li a0, 1
 li a1, 0
 li a2, 0
 li a3, 0
 li a4, 0
 li a5, 0
 li a6, 0
 li a7, SYS_CLOCK_GETRES
 ecall
 bnez t0, native_bad
 bnez a0, native_bad
 bltz a1, native_bad
 li t0, 1000000000
 bgeu a2, t0, native_bad
 bnez a3, native_bad
 bnez a4, native_bad
 bnez a5, native_bad
 bnez a6, native_bad
 bnez a7, native_bad
 li t6, 30
 li a0, 0
 li a1, 0
 li a2, 0
 li a3, 0
 li a4, 0
 li a5, 0
 li a6, 0
 li a7, SYS_CLOCK_GETTIME
 ecall
 bnez t0, native_bad
 li t0, -1
 bne a0, t0, native_bad
 bnez a1, native_bad
 bnez a2, native_bad
 bnez a3, native_bad
 bnez a4, native_bad
 bnez a5, native_bad
 bnez a6, native_bad
 bnez a7, native_bad
 li t6, 1
 li a0, 0
 li a1, 0
 li a2, 0
 li a3, 0
 li a4, 0
 li a5, 0
 li a6, 0
 li a7, 87
 ecall
 bnez t0, native_bad
 li t0, -19
 bne a0, t0, native_bad
 bnez a1, native_bad
 bnez a2, native_bad
 bnez a3, native_bad
 bnez a4, native_bad
 bnez a5, native_bad
 bnez a6, native_bad
 bnez a7, native_bad
 li t6, 2
 li a0, 0
 li a1, 0
 li a2, 0
 li a3, 0
 li a4, 0
 li a5, 0
 li a6, 0
 li a7, SYS_COPY
 ecall
 bnez t0, native_bad
 li t0, -18
 bne a0, t0, native_bad
 bnez a1, native_bad
 bnez a2, native_bad
 bnez a3, native_bad
 bnez a4, native_bad
 bnez a5, native_bad
 bnez a6, native_bad
 bnez a7, native_bad
 li t6, 3
 li a0, 0
 li a1, 0
 li a2, 0
 li a3, 0
 li a4, 0
 li a5, 0
 li a6, 0
 li a2, 1
 li a7, SYS_GETID
 ecall
 bnez t0, native_bad
 li t0, -1
 bne a0, t0, native_bad
 bnez a1, native_bad
 bnez a2, native_bad
 bnez a3, native_bad
 bnez a4, native_bad
 bnez a5, native_bad
 bnez a6, native_bad
 bnez a7, native_bad
 li t6, 4
 mv a0, s1
 li a1, 0
 li a2, 0
 li a3, 0
 li a4, 0
 li a5, 0
 li a6, 0
 li a7, SYS_ALLOC
 ecall
 bnez t0, native_bad
 li t0, -2
 bne a0, t0, native_bad
 bnez a1, native_bad
 bnez a2, native_bad
 bnez a3, native_bad
 bnez a4, native_bad
 bnez a5, native_bad
 bnez a6, native_bad
 bnez a7, native_bad
 li t6, 5
 li t0, 4096
 sd t0, 0(sp)
 li t0, 1
 sd t0, 8(sp)
 sd zero, 16(sp)
 sd zero, 24(sp)
 sd zero, 32(sp)
 sd zero, 40(sp)
 mv a0, s1
 mv a1, sp
 li t0, 1
 sd t0, 40(sp)
 li a2, 0
 li a3, 0
 li a4, 0
 li a5, 0
 li a6, 0
 li a7, SYS_ALLOC
 ecall
 bnez t0, native_bad
 li t0, -1
 bne a0, t0, native_bad
 bnez a1, native_bad
 bnez a2, native_bad
 bnez a3, native_bad
 bnez a4, native_bad
 bnez a5, native_bad
 bnez a6, native_bad
 bnez a7, native_bad
 li t6, 6
 li t0, 4096
 sd t0, 0(sp)
 li t0, 1
 sd t0, 8(sp)
 sd zero, 16(sp)
 sd zero, 24(sp)
 sd zero, 32(sp)
 sd zero, 40(sp)
 mv a0, s1
 mv a1, sp
 li a2, 0
 li a3, 0
 li a4, 0
 li a5, 0
 li a6, 0
 li a7, SYS_ALLOC
 ecall
 bnez t0, native_bad
 li t0, 0
 bne a0, t0, native_bad
 bnez a2, native_bad
 bnez a3, native_bad
 bnez a4, native_bad
 bnez a5, native_bad
 bnez a6, native_bad
 bnez a7, native_bad
 mv s2, a1
 li t6, 7
 sd s2, 64(sp)
 sd zero, 72(sp)
 li t0, 262144
 sd t0, 80(sp)
 li t0, 4096
 sd t0, 88(sp)
 li t0, 3
 sd t0, 96(sp)
 sd zero, 104(sp)
 mv a0, s0
 addi a1, sp, 64
 li a2, 0
 li a3, 0
 li a4, 0
 li a5, 0
 li a6, 0
 li a7, SYS_MAP
 ecall
 bnez t0, native_bad
 li t0, 0
 bne a0, t0, native_bad
 bnez a1, native_bad
 bnez a2, native_bad
 bnez a3, native_bad
 bnez a4, native_bad
 bnez a5, native_bad
 bnez a6, native_bad
 bnez a7, native_bad
 li t0, 262144
 li t1, 305419896
 sd t1, 0(t0)
 mv a0, s2
 li a1, 0
 li a2, 0
 li a3, 0
 li a4, 0
 li a5, 0
 li a6, 0
 li a7, SYS_FREE
 ecall
 bnez t0, native_bad
 li t0, 0
 bne a0, t0, native_bad
 bnez a1, native_bad
 bnez a2, native_bad
 bnez a3, native_bad
 bnez a4, native_bad
 bnez a5, native_bad
 bnez a6, native_bad
 bnez a7, native_bad
 li t0, 262144
 ld t1, 0(t0)
 li t0, 305419896
 bne t1, t0, native_bad
 mv a0, s0
 li a1, 262144
 li a2, 0
 li a3, 0
 li a4, 0
 li a5, 0
 li a6, 0
 li a7, SYS_UNMAP
 ecall
 bnez t0, native_bad
 li t0, 0
 bne a0, t0, native_bad
 bnez a1, native_bad
 bnez a2, native_bad
 bnez a3, native_bad
 bnez a4, native_bad
 bnez a5, native_bad
 bnez a6, native_bad
 bnez a7, native_bad
 li t6, 8
 li a0, 0
 li a1, 0
 li a2, 0
 li a3, 0
 li a4, 0
 li a5, 0
 li a6, 0
 li a7, SYS_YIELD
 ecall
 bnez t0, native_bad
 li t0, 0
 bne a0, t0, native_bad
 bnez a1, native_bad
 bnez a2, native_bad
 bnez a3, native_bad
 bnez a4, native_bad
 bnez a5, native_bad
 bnez a6, native_bad
 bnez a7, native_bad
 fmv.x.d t0, f31
 bne t0, s11, native_bad
 frcsr t0
 li t1, 33
 bne t0, t1, native_bad
 li t6, 9
 li a0, 0
 li a1, 0
 li a2, 0
 li a3, 0
 li a4, 0
 li a5, 0
 li a6, 0
 li a7, SYS_GETID
 ecall
 bnez t0, native_bad
 li t0, 0
 bne a0, t0, native_bad
 bnez a4, native_bad
 bnez a5, native_bad
 bnez a6, native_bad
 bnez a7, native_bad
 li t0, 1
 bne a2, t0, native_bad
 bne a3, t0, native_bad
 srli t0, a1, 59
 li t1, 1
 bne t0, t1, native_bad
 li t6, 10
 li t0, 4096
 sd t0, 0(sp)
 li t0, 3
 sd t0, 8(sp)
 sd zero, 16(sp)
 sd zero, 24(sp)
 sd zero, 32(sp)
 sd zero, 40(sp)
 mv a0, s1
 mv a1, sp
 li a2, 0
 li a3, 0
 li a4, 0
 li a5, 0
 li a6, 0
 li a7, SYS_ALLOC
 ecall
 bnez t0, native_bad
 li t0, 0
 bne a0, t0, native_bad
 bnez a2, native_bad
 bnez a3, native_bad
 bnez a4, native_bad
 bnez a5, native_bad
 bnez a6, native_bad
 bnez a7, native_bad
 mv s3, a1
 mv a0, s2
 li a1, 0
 li a2, 0
 li a3, 0
 li a4, 0
 li a5, 0
 li a6, 0
 li a7, SYS_FREE
 ecall
 bnez t0, native_bad
 li t0, -3
 bne a0, t0, native_bad
 bnez a1, native_bad
 bnez a2, native_bad
 bnez a3, native_bad
 bnez a4, native_bad
 bnez a5, native_bad
 bnez a6, native_bad
 bnez a7, native_bad
 li t6, 11
 sd s3, 64(sp)
 sd zero, 72(sp)
 li t0, 327680
 sd t0, 80(sp)
 li t0, 4096
 sd t0, 88(sp)
 li t0, 3
 sd t0, 96(sp)
 sd zero, 104(sp)
 mv a0, s0
 addi a1, sp, 64
 li a2, 0
 li a3, 0
 li a4, 0
 li a5, 0
 li a6, 0
 li a7, SYS_MAP
 ecall
 bnez t0, native_bad
 li t0, 0
 bne a0, t0, native_bad
 bnez a1, native_bad
 bnez a2, native_bad
 bnez a3, native_bad
 bnez a4, native_bad
 bnez a5, native_bad
 bnez a6, native_bad
 bnez a7, native_bad
 li t0, 327680
 li t1, 44041491
 sw t1, 0(t0)
 li t1, 32871
 sw t1, 4(t0)
 mv a0, s3
 li a1, 0
 li a2, 0
 li a3, 0
 li a4, 0
 li a5, 0
 li a6, 0
 li a7, SYS_SEAL
 ecall
 bnez t0, native_bad
 li t0, -7
 bne a0, t0, native_bad
 bnez a1, native_bad
 bnez a2, native_bad
 bnez a3, native_bad
 bnez a4, native_bad
 bnez a5, native_bad
 bnez a6, native_bad
 bnez a7, native_bad
 mv a0, s0
 li a1, 327680
 li a2, 0
 li a3, 0
 li a4, 0
 li a5, 0
 li a6, 0
 li a7, SYS_UNMAP
 ecall
 bnez t0, native_bad
 li t0, 0
 bne a0, t0, native_bad
 bnez a1, native_bad
 bnez a2, native_bad
 bnez a3, native_bad
 bnez a4, native_bad
 bnez a5, native_bad
 bnez a6, native_bad
 bnez a7, native_bad
 mv a0, s3
 li a1, 0
 li a2, 0
 li a3, 0
 li a4, 0
 li a5, 0
 li a6, 0
 li a7, SYS_SEAL
 ecall
 bnez t0, native_bad
 li t0, 0
 bne a0, t0, native_bad
 bnez a1, native_bad
 bnez a2, native_bad
 bnez a3, native_bad
 bnez a4, native_bad
 bnez a5, native_bad
 bnez a6, native_bad
 bnez a7, native_bad
 li t6, 12
 sd s3, 64(sp)
 sd zero, 72(sp)
 li t0, 327680
 sd t0, 80(sp)
 li t0, 4096
 sd t0, 88(sp)
 li t0, 3
 sd t0, 96(sp)
 sd zero, 104(sp)
 mv a0, s0
 addi a1, sp, 64
 li a2, 0
 li a3, 0
 li a4, 0
 li a5, 0
 li a6, 0
 li a7, SYS_MAP
 ecall
 bnez t0, native_bad
 li t0, -5
 bne a0, t0, native_bad
 bnez a1, native_bad
 bnez a2, native_bad
 bnez a3, native_bad
 bnez a4, native_bad
 bnez a5, native_bad
 bnez a6, native_bad
 bnez a7, native_bad
 sd s3, 64(sp)
 sd zero, 72(sp)
 li t0, 327680
 sd t0, 80(sp)
 li t0, 4096
 sd t0, 88(sp)
 li t0, 5
 sd t0, 96(sp)
 sd zero, 104(sp)
 mv a0, s0
 addi a1, sp, 64
 li a2, 0
 li a3, 0
 li a4, 0
 li a5, 0
 li a6, 0
 li a7, SYS_MAP
 ecall
 bnez t0, native_bad
 li t0, 0
 bne a0, t0, native_bad
 bnez a1, native_bad
 bnez a2, native_bad
 bnez a3, native_bad
 bnez a4, native_bad
 bnez a5, native_bad
 bnez a6, native_bad
 bnez a7, native_bad
 li t0, 327680
 jalr ra, t0, 0
 li t0, 42
 bne a0, t0, native_bad
 li t6, 13
 mv a0, s3
 li a1, 0
 li a2, 0
 li a3, 0
 li a4, 0
 li a5, 0
 li a6, 0
 li a7, SYS_FREE
 ecall
 bnez t0, native_bad
 li t0, 0
 bne a0, t0, native_bad
 bnez a1, native_bad
 bnez a2, native_bad
 bnez a3, native_bad
 bnez a4, native_bad
 bnez a5, native_bad
 bnez a6, native_bad
 bnez a7, native_bad
 li t0, 327680
 jalr ra, t0, 0
 li t0, 42
 bne a0, t0, native_bad
 mv a0, s0
 li a1, 327680
 li a2, 0
 li a3, 0
 li a4, 0
 li a5, 0
 li a6, 0
 li a7, SYS_UNMAP
 ecall
 bnez t0, native_bad
 li t0, 0
 bne a0, t0, native_bad
 bnez a1, native_bad
 bnez a2, native_bad
 bnez a3, native_bad
 bnez a4, native_bad
 bnez a5, native_bad
 bnez a6, native_bad
 bnez a7, native_bad
 li t6, 14
 fmv.x.d t0, f31
 bne t0, s11, native_bad
 beqz s8, native_exit
 li t0, 262144
 ld t1, 0(t0)
native_exit:
 li a0, 42
 li a1, 0
 li a2, 0
 li a3, 0
 li a4, 0
 li a5, 0
 li a6, 0
 li a7, SYS_EXIT
 ecall
native_bad:
 mv a0, t6
 li a1, 0
 li a2, 0
 li a3, 0
 li a4, 0
 li a5, 0
 li a6, 0
 li a7, SYS_EXIT
 ecall
 j native_bad
native_end:
