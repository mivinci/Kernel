.arch armv8-a+fp+simd
#include <arm64/context.inc>
.section .text,"ax"
.global hal_trap_init
hal_trap_init:
 adr x9, vectors
 msr vbar_el1, x9
 adrp x9, __emergency_top
 add x9, x9, :lo12:__emergency_top
 msr sp_el0, x9
 mrs x9, cpacr_el1
 orr x9, x9, #0x300000
 msr cpacr_el1, x9
 isb
 ret
.global kernelirqentry
kernelirqentry:
 sub sp, sp, #HC_SIZE
 arch_save_gprs
 add x9, sp, #HC_SIZE
 str x9, [sp, #TF_SP]
 mrs x9, elr_el1
 // An IRQ before the idle WFI skips that WFI on return.
 adr x10, idlewait
 cmp x9, x10
 b.ne 1f
 adr x9, idleresume
1: str x9, [sp, #TF_PC]
 mrs x9, spsr_el1
 str x9, [sp, #TF_STATUS]
 mov x9, #0x8000000000000000
 str x9, [sp, #TF_CAUSE]
 str xzr, [sp, #TF_ADDRESS]
 arch_save_fp
 mov x0, sp
 bl kerneltrap
 arch_restore_fp sp
 ldr x9, [sp, #TF_PC]
 msr elr_el1, x9
 ldr x9, [sp, #TF_STATUS]
 msr spsr_el1, x9
 arch_restore_gprs sp
 add sp, sp, #HC_SIZE
 eret
.global hal_idle
hal_idle:
 msr daifclr, #2
idlewait:
 wfi
idleresume:
 msr daifset, #2
 ret
.global hal_irq_enable
hal_irq_enable:
 msr daifclr, #2
 ret
