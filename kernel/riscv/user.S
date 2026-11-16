#include <riscv/context.inc>
.section .text,"ax"
.option norvc
.option norelax
.global hal_user_enter
hal_user_enter:
 mv t6, a0
 csrw sscratch, a0
 la t0, userentry
 csrw stvec, t0
 li t0, 0x6000
 csrs sstatus, t0
 arch_restore_fp t6
 ld t0, TF_PC(t6)
 csrw sepc, t0
 ld t0, TF_STATUS(t6)
 csrw sstatus, t0
 arch_restore_gprs t6
 sret
.balign 4
userentry:
 csrrw sp, sscratch, sp
 arch_save_gprs
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
 la t0, nesteduser
 csrw stvec, t0
 arch_save_fp
 la t0, __emergency_top
 csrw sscratch, t0
 la t0, kernelvectors
 ori t0, t0, 1
 csrw stvec, t0
 mv a0, sp
 call usertrap
 j hal_halt
nesteduser:
 j hal_halt
