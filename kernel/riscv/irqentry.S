#include <riscv/context.inc>
.section .text,"ax"
.option norvc
.option norelax
.global hal_trap_init
hal_trap_init:
 la t0, kernelvectors
 ori t0, t0, 1
 csrw stvec, t0
 la t0, __emergency_top
 csrw sscratch, t0
 li t0, 0x6000
 csrs sstatus, t0
 ret
.balign 256
.global kernelvectors
kernelvectors:
 // Vectored interrupts preserve the synchronous emergency entry.
 j trapentry
 .rept 4
 j hal_halt
 .endr
 j kernelirqentry
 .rept 3
 j hal_halt
 .endr
 j kernelirqentry
 .rept 6
 j hal_halt
 .endr
kernelirqentry:
 addi sp, sp, -HC_SIZE
 arch_save_gprs
 addi t0, sp, HC_SIZE
 sd t0, TF_SP(sp)
 csrr t0, sepc
 // An IRQ before the idle WFI skips that WFI on return.
 la t1, idlewait
 bne t0, t1, 1f
 la t0, idleresume
1: sd t0, TF_PC(sp)
 csrr t0, sstatus
 sd t0, TF_STATUS(sp)
 csrr t0, scause
 sd t0, TF_CAUSE(sp)
 sd zero, TF_ADDRESS(sp)
 arch_save_fp
 mv a0, sp
 call kerneltrap
 arch_restore_fp sp
 ld t0, TF_PC(sp)
 csrw sepc, t0
 ld t0, TF_STATUS(sp)
 csrw sstatus, t0
 arch_restore_gprs sp
 sret
.global hal_idle
hal_idle:
 csrsi sstatus, 2
idlewait:
 wfi
idleresume:
 csrci sstatus, 2
 ret
.global hal_irq_enable
hal_irq_enable:
 csrsi sstatus, 2
 ret
