#ifndef _ARM64_CONTEXT_ASM_H
#define _ARM64_CONTEXT_ASM_H
#ifdef __ASSEMBLER__
#include <arm64/context.h>
.macro arch_save_gprs
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
.endm
.macro arch_save_fp
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
.endm
.macro arch_restore_fp base
 ldp q0, q1, [\base, #(HC_FP+FP_REG(0))]
 ldp q2, q3, [\base, #(HC_FP+FP_REG(2))]
 ldp q4, q5, [\base, #(HC_FP+FP_REG(4))]
 ldp q6, q7, [\base, #(HC_FP+FP_REG(6))]
 ldp q8, q9, [\base, #(HC_FP+FP_REG(8))]
 ldp q10, q11, [\base, #(HC_FP+FP_REG(10))]
 ldp q12, q13, [\base, #(HC_FP+FP_REG(12))]
 ldp q14, q15, [\base, #(HC_FP+FP_REG(14))]
 ldp q16, q17, [\base, #(HC_FP+FP_REG(16))]
 ldp q18, q19, [\base, #(HC_FP+FP_REG(18))]
 ldp q20, q21, [\base, #(HC_FP+FP_REG(20))]
 ldp q22, q23, [\base, #(HC_FP+FP_REG(22))]
 ldp q24, q25, [\base, #(HC_FP+FP_REG(24))]
 ldp q26, q27, [\base, #(HC_FP+FP_REG(26))]
 ldp q28, q29, [\base, #(HC_FP+FP_REG(28))]
 ldp q30, q31, [\base, #(HC_FP+FP_REG(30))]
 ldr w9, [\base, #(HC_FP+FP_CONTROL)]
 msr fpsr, x9
 ldr w9, [\base, #(HC_FP+FP_CONTROL+4)]
 msr fpcr, x9
.endm
.macro arch_restore_gprs base
 ldp x0, x1, [\base, #TF_REG(0)]
 ldp x2, x3, [\base, #TF_REG(2)]
 ldp x4, x5, [\base, #TF_REG(4)]
 ldp x6, x7, [\base, #TF_REG(6)]
 ldp x8, x9, [\base, #TF_REG(8)]
 ldp x10, x11, [\base, #TF_REG(10)]
 ldp x12, x13, [\base, #TF_REG(12)]
 ldp x14, x15, [\base, #TF_REG(14)]
 ldp x16, x17, [\base, #TF_REG(16)]
 ldp x18, x19, [\base, #TF_REG(18)]
 ldp x20, x21, [\base, #TF_REG(20)]
 ldp x22, x23, [\base, #TF_REG(22)]
 ldp x24, x25, [\base, #TF_REG(24)]
 ldp x26, x27, [\base, #TF_REG(26)]
 ldp x28, x29, [\base, #TF_REG(28)]
 ldr x30, [\base, #TF_REG(30)]
.endm
#endif
#endif
