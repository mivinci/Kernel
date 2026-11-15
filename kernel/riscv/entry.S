#include <riscv/trap.h>
.section .text.boot,"ax"
.option norvc
.option norelax
.global _start
_start:
 csrci sstatus, 2
 csrw sie, zero
 bnez a0, hal_halt
 la t0, __emergency_top
 csrw sscratch, t0
 la t0, trapentry
 csrw stvec, t0
 la sp, __stack_top
 la t0, __bss_start
 la t1, __bss_end
1: bgeu t0, t1, 2f
 sd zero, 0(t0)
 addi t0, t0, 8
 j 1b
2: call arch_start
 j hal_halt
.balign 4
.global trapentry
trapentry:
 csrrw sp, sscratch, sp
 addi sp, sp, -TF_SIZE
 sd zero, TF_REG(0)(sp)
 sd x1, TF_REG(1)(sp)
 sd x3, TF_REG(3)(sp)
 sd x4, TF_REG(4)(sp)
 sd x5, TF_REG(5)(sp)
 sd x6, TF_REG(6)(sp)
 sd x7, TF_REG(7)(sp)
 sd x8, TF_REG(8)(sp)
 sd x9, TF_REG(9)(sp)
 sd x10, TF_REG(10)(sp)
 sd x11, TF_REG(11)(sp)
 sd x12, TF_REG(12)(sp)
 sd x13, TF_REG(13)(sp)
 sd x14, TF_REG(14)(sp)
 sd x15, TF_REG(15)(sp)
 sd x16, TF_REG(16)(sp)
 sd x17, TF_REG(17)(sp)
 sd x18, TF_REG(18)(sp)
 sd x19, TF_REG(19)(sp)
 sd x20, TF_REG(20)(sp)
 sd x21, TF_REG(21)(sp)
 sd x22, TF_REG(22)(sp)
 sd x23, TF_REG(23)(sp)
 sd x24, TF_REG(24)(sp)
 sd x25, TF_REG(25)(sp)
 sd x26, TF_REG(26)(sp)
 sd x27, TF_REG(27)(sp)
 sd x28, TF_REG(28)(sp)
 sd x29, TF_REG(29)(sp)
 sd x30, TF_REG(30)(sp)
 sd x31, TF_REG(31)(sp)
 csrr t0, sscratch
 sd t0, TF_SP(sp)
 csrr t0, sepc
 sd t0, TF_PC(sp)
 csrr t0, sstatus
 sd t0, TF_STATUS(sp)
 csrr t0, scause
 sd t0, TF_CAUSE(sp)
 csrr t0, stval
 sd t0, TF_ADDRESS(sp)
 la t0, nested
 csrw stvec, t0
 mv a0, sp
 call arch_trap
 la t0, trapentry
 csrw stvec, t0
 ld t0, TF_PC(sp)
 csrw sepc, t0
 ld t0, TF_STATUS(sp)
 csrw sstatus, t0
 la t0, __emergency_top
 csrw sscratch, t0
 ld x1, TF_REG(1)(sp)
 ld x3, TF_REG(3)(sp)
 ld x4, TF_REG(4)(sp)
 ld x5, TF_REG(5)(sp)
 ld x6, TF_REG(6)(sp)
 ld x7, TF_REG(7)(sp)
 ld x8, TF_REG(8)(sp)
 ld x9, TF_REG(9)(sp)
 ld x10, TF_REG(10)(sp)
 ld x11, TF_REG(11)(sp)
 ld x12, TF_REG(12)(sp)
 ld x13, TF_REG(13)(sp)
 ld x14, TF_REG(14)(sp)
 ld x15, TF_REG(15)(sp)
 ld x16, TF_REG(16)(sp)
 ld x17, TF_REG(17)(sp)
 ld x18, TF_REG(18)(sp)
 ld x19, TF_REG(19)(sp)
 ld x20, TF_REG(20)(sp)
 ld x21, TF_REG(21)(sp)
 ld x22, TF_REG(22)(sp)
 ld x23, TF_REG(23)(sp)
 ld x24, TF_REG(24)(sp)
 ld x25, TF_REG(25)(sp)
 ld x26, TF_REG(26)(sp)
 ld x27, TF_REG(27)(sp)
 ld x28, TF_REG(28)(sp)
 ld x29, TF_REG(29)(sp)
 ld x30, TF_REG(30)(sp)
 ld x31, TF_REG(31)(sp)
 ld sp, TF_SP(sp)
 sret
nested:
 csrci sstatus, 2
 j hal_halt
.global hal_halt
hal_halt:
 csrci sstatus, 2
1: wfi
 j 1b
