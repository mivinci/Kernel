.arch armv8-a+fp+simd
#include <arm64/context.h>
.section .text,"ax"
.global hal_user_enter
hal_user_enter:
 mov x30, x0
 mov sp, x0
 mrs x9, cpacr_el1
 orr x9, x9, #0x300000
 msr cpacr_el1, x9
 adr x9, uservectors
 msr vbar_el1, x9
 isb
 ldp q0, q1, [x30, #(HC_FP+FP_REG(0))]
 ldp q2, q3, [x30, #(HC_FP+FP_REG(2))]
 ldp q4, q5, [x30, #(HC_FP+FP_REG(4))]
 ldp q6, q7, [x30, #(HC_FP+FP_REG(6))]
 ldp q8, q9, [x30, #(HC_FP+FP_REG(8))]
 ldp q10, q11, [x30, #(HC_FP+FP_REG(10))]
 ldp q12, q13, [x30, #(HC_FP+FP_REG(12))]
 ldp q14, q15, [x30, #(HC_FP+FP_REG(14))]
 ldp q16, q17, [x30, #(HC_FP+FP_REG(16))]
 ldp q18, q19, [x30, #(HC_FP+FP_REG(18))]
 ldp q20, q21, [x30, #(HC_FP+FP_REG(20))]
 ldp q22, q23, [x30, #(HC_FP+FP_REG(22))]
 ldp q24, q25, [x30, #(HC_FP+FP_REG(24))]
 ldp q26, q27, [x30, #(HC_FP+FP_REG(26))]
 ldp q28, q29, [x30, #(HC_FP+FP_REG(28))]
 ldp q30, q31, [x30, #(HC_FP+FP_REG(30))]
 ldr w9, [x30, #(HC_FP+FP_CONTROL)]
 msr fpsr, x9
 ldr w9, [x30, #(HC_FP+FP_CONTROL+4)]
 msr fpcr, x9
 ldr x9, [x30, #TF_SP]
 msr sp_el0, x9
 ldr x9, [x30, #TF_PC]
 msr elr_el1, x9
 ldr x9, [x30, #TF_STATUS]
 msr spsr_el1, x9
 ldp x0, x1, [x30, #TF_REG(0)]
 ldp x2, x3, [x30, #TF_REG(2)]
 ldp x4, x5, [x30, #TF_REG(4)]
 ldp x6, x7, [x30, #TF_REG(6)]
 ldp x8, x9, [x30, #TF_REG(8)]
 ldp x10, x11, [x30, #TF_REG(10)]
 ldp x12, x13, [x30, #TF_REG(12)]
 ldp x14, x15, [x30, #TF_REG(14)]
 ldp x16, x17, [x30, #TF_REG(16)]
 ldp x18, x19, [x30, #TF_REG(18)]
 ldp x20, x21, [x30, #TF_REG(20)]
 ldp x22, x23, [x30, #TF_REG(22)]
 ldp x24, x25, [x30, #TF_REG(24)]
 ldp x26, x27, [x30, #TF_REG(26)]
 ldp x28, x29, [x30, #TF_REG(28)]
 ldr x30, [x30, #TF_REG(30)]
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
userentry:
 stp x0, x1, [sp, #TF_REG(0)]
 stp x2, x3, [sp, #TF_REG(2)]
 stp x4, x5, [sp, #TF_REG(4)]
 stp x6, x7, [sp, #TF_REG(6)]
 stp x8, x9, [sp, #TF_REG(8)]
 stp x10, x11, [sp, #TF_REG(10)]
 stp x12, x13, [sp, #TF_REG(12)]
 stp x14, x15, [sp, #TF_REG(14)]
 stp x16, x17, [sp, #TF_REG(16)]
 stp x18, x19, [sp, #TF_REG(18)]
 stp x20, x21, [sp, #TF_REG(20)]
 stp x22, x23, [sp, #TF_REG(22)]
 stp x24, x25, [sp, #TF_REG(24)]
 stp x26, x27, [sp, #TF_REG(26)]
 stp x28, x29, [sp, #TF_REG(28)]
 str x30, [sp, #TF_REG(30)]
 mrs x9, sp_el0
 str x9, [sp, #TF_SP]
 mrs x9, elr_el1
 str x9, [sp, #TF_PC]
 mrs x9, spsr_el1
 str x9, [sp, #TF_STATUS]
 mrs x9, esr_el1
 str x9, [sp, #TF_CAUSE]
 mrs x9, far_el1
 str x9, [sp, #TF_ADDRESS]
 stp q0, q1, [sp, #(HC_FP+FP_REG(0))]
 stp q2, q3, [sp, #(HC_FP+FP_REG(2))]
 stp q4, q5, [sp, #(HC_FP+FP_REG(4))]
 stp q6, q7, [sp, #(HC_FP+FP_REG(6))]
 stp q8, q9, [sp, #(HC_FP+FP_REG(8))]
 stp q10, q11, [sp, #(HC_FP+FP_REG(10))]
 stp q12, q13, [sp, #(HC_FP+FP_REG(12))]
 stp q14, q15, [sp, #(HC_FP+FP_REG(14))]
 stp q16, q17, [sp, #(HC_FP+FP_REG(16))]
 stp q18, q19, [sp, #(HC_FP+FP_REG(18))]
 stp q20, q21, [sp, #(HC_FP+FP_REG(20))]
 stp q22, q23, [sp, #(HC_FP+FP_REG(22))]
 stp q24, q25, [sp, #(HC_FP+FP_REG(24))]
 stp q26, q27, [sp, #(HC_FP+FP_REG(26))]
 stp q28, q29, [sp, #(HC_FP+FP_REG(28))]
 stp q30, q31, [sp, #(HC_FP+FP_REG(30))]
 mrs x9, fpsr
 str w9, [sp, #(HC_FP+FP_CONTROL)]
 mrs x9, fpcr
 str w9, [sp, #(HC_FP+FP_CONTROL+4)]
 str xzr, [sp, #(HC_FP+FP_RESERVED)]
 ldr x9, =__emergency_top
 msr sp_el0, x9
 adr x9, vectors
 msr vbar_el1, x9
 isb
 mov x0, sp
 bl usertrap
 b hal_halt
