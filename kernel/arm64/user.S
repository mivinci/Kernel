.arch armv8-a+fp+simd
#include <arm64/context.inc>
.section .text,"ax"
.global hal_user_enter
hal_user_enter:
 str x0, [sp, #-16]!
 bl procuserenter
 ldr x0, [sp], #16
 mov x30, x0
 mov sp, x0
 mrs x9, cpacr_el1
 orr x9, x9, #0x300000
 msr cpacr_el1, x9
 adr x9, uservectors
 msr vbar_el1, x9
 isb
 arch_restore_fp x30
 ldr x9, [x30, #TF_SP]
 msr sp_el0, x9
 ldr x9, [x30, #TF_PC]
 msr elr_el1, x9
 ldr x9, [x30, #TF_STATUS]
 msr spsr_el1, x9
 arch_restore_gprs x30
 eret
.balign 2048
uservectors:
 b hal_halt
 .balign 128
 b hal_halt
 .balign 128
 b hal_halt
 .balign 128
 b hal_halt
 .balign 128
 b hal_halt
 .balign 128
 b hal_halt
 .balign 128
 b hal_halt
 .balign 128
 b hal_halt
 .balign 128
 b userentry
 .balign 128
 b userirqentry
 .balign 128
 b hal_halt
 .balign 128
 b hal_halt
 .balign 128
 b hal_halt
 .balign 128
 b hal_halt
 .balign 128
 b hal_halt
 .balign 128
 b hal_halt
 .balign 128
userentry:
 arch_save_gprs
 mrs x9, esr_el1
 str x9, [sp, #TF_CAUSE]
 b userstate
userirqentry:
 arch_save_gprs
 mov x9, #0x8000000000000000
 str x9, [sp, #TF_CAUSE]
userstate:
 mrs x9, sp_el0
 str x9, [sp, #TF_SP]
 mrs x9, elr_el1
 str x9, [sp, #TF_PC]
 mrs x9, spsr_el1
 str x9, [sp, #TF_STATUS]
 mrs x9, far_el1
 str x9, [sp, #TF_ADDRESS]
 arch_save_fp
 adrp x9, __emergency_top
 add x9, x9, :lo12:__emergency_top
 msr sp_el0, x9
 adr x9, vectors
 msr vbar_el1, x9
 isb
 mov x0, sp
 bl usertrap
 b hal_halt
