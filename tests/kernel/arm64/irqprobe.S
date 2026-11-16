#include <arm64/context.inc>
.section .text,"ax"
.global arch_irqprobe
arch_irqprobe:
.arch armv8-a+fp+simd
 sub sp, sp, #HC_SIZE
 arch_save_gprs
 arch_save_fp
 mov x30, #32768
 dup v0.2d, x30
 mov x30, #32799
 dup v1.2d, x30
 mov x30, #32830
 dup v2.2d, x30
 mov x30, #32861
 dup v3.2d, x30
 mov x30, #32892
 dup v4.2d, x30
 mov x30, #32923
 dup v5.2d, x30
 mov x30, #32954
 dup v6.2d, x30
 mov x30, #32985
 dup v7.2d, x30
 mov x30, #33016
 dup v8.2d, x30
 mov x30, #33047
 dup v9.2d, x30
 mov x30, #33078
 dup v10.2d, x30
 mov x30, #33109
 dup v11.2d, x30
 mov x30, #33140
 dup v12.2d, x30
 mov x30, #33171
 dup v13.2d, x30
 mov x30, #33202
 dup v14.2d, x30
 mov x30, #33233
 dup v15.2d, x30
 mov x30, #33264
 dup v16.2d, x30
 mov x30, #33295
 dup v17.2d, x30
 mov x30, #33326
 dup v18.2d, x30
 mov x30, #33357
 dup v19.2d, x30
 mov x30, #33388
 dup v20.2d, x30
 mov x30, #33419
 dup v21.2d, x30
 mov x30, #33450
 dup v22.2d, x30
 mov x30, #33481
 dup v23.2d, x30
 mov x30, #33512
 dup v24.2d, x30
 mov x30, #33543
 dup v25.2d, x30
 mov x30, #33574
 dup v26.2d, x30
 mov x30, #33605
 dup v27.2d, x30
 mov x30, #33636
 dup v28.2d, x30
 mov x30, #33667
 dup v29.2d, x30
 mov x30, #33698
 dup v30.2d, x30
 mov x30, #33729
 dup v31.2d, x30
 mov x30, #31
 msr fpsr, x30
 mov x30, #0x400000
 msr fpcr, x30
 mov x30, #0xa0000000
 msr nzcv, x30
 mov x0, #4096
 mov x1, #4113
 mov x2, #4130
 mov x3, #4147
 mov x4, #4164
 mov x5, #4181
 mov x6, #4198
 mov x7, #4215
 mov x8, #4232
 mov x9, #4249
 mov x10, #4266
 mov x11, #4283
 mov x12, #4300
 mov x13, #4317
 mov x14, #4334
 mov x15, #4351
 mov x16, #4368
 mov x17, #4385
 mov x18, #4402
 mov x19, #4419
 mov x20, #4436
 mov x21, #4453
 mov x22, #4470
 mov x23, #4487
 mov x24, #4504
 mov x25, #4521
 mov x26, #4538
 mov x27, #4555
 mov x28, #4572
 mov x29, #4589
 bl hal_idle
 mrs x30, nzcv
 str x30, [sp, #TF_ADDRESS]
 mov x30, #4096
 cmp x0, x30
 b.ne irqprobe_bad
 mov x30, #4113
 cmp x1, x30
 b.ne irqprobe_bad
 mov x30, #4130
 cmp x2, x30
 b.ne irqprobe_bad
 mov x30, #4147
 cmp x3, x30
 b.ne irqprobe_bad
 mov x30, #4164
 cmp x4, x30
 b.ne irqprobe_bad
 mov x30, #4181
 cmp x5, x30
 b.ne irqprobe_bad
 mov x30, #4198
 cmp x6, x30
 b.ne irqprobe_bad
 mov x30, #4215
 cmp x7, x30
 b.ne irqprobe_bad
 mov x30, #4232
 cmp x8, x30
 b.ne irqprobe_bad
 mov x30, #4249
 cmp x9, x30
 b.ne irqprobe_bad
 mov x30, #4266
 cmp x10, x30
 b.ne irqprobe_bad
 mov x30, #4283
 cmp x11, x30
 b.ne irqprobe_bad
 mov x30, #4300
 cmp x12, x30
 b.ne irqprobe_bad
 mov x30, #4317
 cmp x13, x30
 b.ne irqprobe_bad
 mov x30, #4334
 cmp x14, x30
 b.ne irqprobe_bad
 mov x30, #4351
 cmp x15, x30
 b.ne irqprobe_bad
 mov x30, #4368
 cmp x16, x30
 b.ne irqprobe_bad
 mov x30, #4385
 cmp x17, x30
 b.ne irqprobe_bad
 mov x30, #4402
 cmp x18, x30
 b.ne irqprobe_bad
 mov x30, #4419
 cmp x19, x30
 b.ne irqprobe_bad
 mov x30, #4436
 cmp x20, x30
 b.ne irqprobe_bad
 mov x30, #4453
 cmp x21, x30
 b.ne irqprobe_bad
 mov x30, #4470
 cmp x22, x30
 b.ne irqprobe_bad
 mov x30, #4487
 cmp x23, x30
 b.ne irqprobe_bad
 mov x30, #4504
 cmp x24, x30
 b.ne irqprobe_bad
 mov x30, #4521
 cmp x25, x30
 b.ne irqprobe_bad
 mov x30, #4538
 cmp x26, x30
 b.ne irqprobe_bad
 mov x30, #4555
 cmp x27, x30
 b.ne irqprobe_bad
 mov x30, #4572
 cmp x28, x30
 b.ne irqprobe_bad
 mov x30, #4589
 cmp x29, x30
 b.ne irqprobe_bad
 ldr x30, [sp, #TF_ADDRESS]
 mov x29, #0xa0000000
 cmp x30, x29
 b.ne irqprobe_bad
 mrs x30, fpsr
 cmp x30, #31
 b.ne irqprobe_bad
 mrs x30, fpcr
 mov x29, #0x400000
 cmp x30, x29
 b.ne irqprobe_bad
 umov x30, v0.d[0]
 mov x29, #32768
 cmp x30, x29
 b.ne irqprobe_bad
 umov x30, v0.d[1]
 mov x29, #32768
 cmp x30, x29
 b.ne irqprobe_bad
 umov x30, v1.d[0]
 mov x29, #32799
 cmp x30, x29
 b.ne irqprobe_bad
 umov x30, v1.d[1]
 mov x29, #32799
 cmp x30, x29
 b.ne irqprobe_bad
 umov x30, v2.d[0]
 mov x29, #32830
 cmp x30, x29
 b.ne irqprobe_bad
 umov x30, v2.d[1]
 mov x29, #32830
 cmp x30, x29
 b.ne irqprobe_bad
 umov x30, v3.d[0]
 mov x29, #32861
 cmp x30, x29
 b.ne irqprobe_bad
 umov x30, v3.d[1]
 mov x29, #32861
 cmp x30, x29
 b.ne irqprobe_bad
 umov x30, v4.d[0]
 mov x29, #32892
 cmp x30, x29
 b.ne irqprobe_bad
 umov x30, v4.d[1]
 mov x29, #32892
 cmp x30, x29
 b.ne irqprobe_bad
 umov x30, v5.d[0]
 mov x29, #32923
 cmp x30, x29
 b.ne irqprobe_bad
 umov x30, v5.d[1]
 mov x29, #32923
 cmp x30, x29
 b.ne irqprobe_bad
 umov x30, v6.d[0]
 mov x29, #32954
 cmp x30, x29
 b.ne irqprobe_bad
 umov x30, v6.d[1]
 mov x29, #32954
 cmp x30, x29
 b.ne irqprobe_bad
 umov x30, v7.d[0]
 mov x29, #32985
 cmp x30, x29
 b.ne irqprobe_bad
 umov x30, v7.d[1]
 mov x29, #32985
 cmp x30, x29
 b.ne irqprobe_bad
 umov x30, v8.d[0]
 mov x29, #33016
 cmp x30, x29
 b.ne irqprobe_bad
 umov x30, v8.d[1]
 mov x29, #33016
 cmp x30, x29
 b.ne irqprobe_bad
 umov x30, v9.d[0]
 mov x29, #33047
 cmp x30, x29
 b.ne irqprobe_bad
 umov x30, v9.d[1]
 mov x29, #33047
 cmp x30, x29
 b.ne irqprobe_bad
 umov x30, v10.d[0]
 mov x29, #33078
 cmp x30, x29
 b.ne irqprobe_bad
 umov x30, v10.d[1]
 mov x29, #33078
 cmp x30, x29
 b.ne irqprobe_bad
 umov x30, v11.d[0]
 mov x29, #33109
 cmp x30, x29
 b.ne irqprobe_bad
 umov x30, v11.d[1]
 mov x29, #33109
 cmp x30, x29
 b.ne irqprobe_bad
 umov x30, v12.d[0]
 mov x29, #33140
 cmp x30, x29
 b.ne irqprobe_bad
 umov x30, v12.d[1]
 mov x29, #33140
 cmp x30, x29
 b.ne irqprobe_bad
 umov x30, v13.d[0]
 mov x29, #33171
 cmp x30, x29
 b.ne irqprobe_bad
 umov x30, v13.d[1]
 mov x29, #33171
 cmp x30, x29
 b.ne irqprobe_bad
 umov x30, v14.d[0]
 mov x29, #33202
 cmp x30, x29
 b.ne irqprobe_bad
 umov x30, v14.d[1]
 mov x29, #33202
 cmp x30, x29
 b.ne irqprobe_bad
 umov x30, v15.d[0]
 mov x29, #33233
 cmp x30, x29
 b.ne irqprobe_bad
 umov x30, v15.d[1]
 mov x29, #33233
 cmp x30, x29
 b.ne irqprobe_bad
 umov x30, v16.d[0]
 mov x29, #33264
 cmp x30, x29
 b.ne irqprobe_bad
 umov x30, v16.d[1]
 mov x29, #33264
 cmp x30, x29
 b.ne irqprobe_bad
 umov x30, v17.d[0]
 mov x29, #33295
 cmp x30, x29
 b.ne irqprobe_bad
 umov x30, v17.d[1]
 mov x29, #33295
 cmp x30, x29
 b.ne irqprobe_bad
 umov x30, v18.d[0]
 mov x29, #33326
 cmp x30, x29
 b.ne irqprobe_bad
 umov x30, v18.d[1]
 mov x29, #33326
 cmp x30, x29
 b.ne irqprobe_bad
 umov x30, v19.d[0]
 mov x29, #33357
 cmp x30, x29
 b.ne irqprobe_bad
 umov x30, v19.d[1]
 mov x29, #33357
 cmp x30, x29
 b.ne irqprobe_bad
 umov x30, v20.d[0]
 mov x29, #33388
 cmp x30, x29
 b.ne irqprobe_bad
 umov x30, v20.d[1]
 mov x29, #33388
 cmp x30, x29
 b.ne irqprobe_bad
 umov x30, v21.d[0]
 mov x29, #33419
 cmp x30, x29
 b.ne irqprobe_bad
 umov x30, v21.d[1]
 mov x29, #33419
 cmp x30, x29
 b.ne irqprobe_bad
 umov x30, v22.d[0]
 mov x29, #33450
 cmp x30, x29
 b.ne irqprobe_bad
 umov x30, v22.d[1]
 mov x29, #33450
 cmp x30, x29
 b.ne irqprobe_bad
 umov x30, v23.d[0]
 mov x29, #33481
 cmp x30, x29
 b.ne irqprobe_bad
 umov x30, v23.d[1]
 mov x29, #33481
 cmp x30, x29
 b.ne irqprobe_bad
 umov x30, v24.d[0]
 mov x29, #33512
 cmp x30, x29
 b.ne irqprobe_bad
 umov x30, v24.d[1]
 mov x29, #33512
 cmp x30, x29
 b.ne irqprobe_bad
 umov x30, v25.d[0]
 mov x29, #33543
 cmp x30, x29
 b.ne irqprobe_bad
 umov x30, v25.d[1]
 mov x29, #33543
 cmp x30, x29
 b.ne irqprobe_bad
 umov x30, v26.d[0]
 mov x29, #33574
 cmp x30, x29
 b.ne irqprobe_bad
 umov x30, v26.d[1]
 mov x29, #33574
 cmp x30, x29
 b.ne irqprobe_bad
 umov x30, v27.d[0]
 mov x29, #33605
 cmp x30, x29
 b.ne irqprobe_bad
 umov x30, v27.d[1]
 mov x29, #33605
 cmp x30, x29
 b.ne irqprobe_bad
 umov x30, v28.d[0]
 mov x29, #33636
 cmp x30, x29
 b.ne irqprobe_bad
 umov x30, v28.d[1]
 mov x29, #33636
 cmp x30, x29
 b.ne irqprobe_bad
 umov x30, v29.d[0]
 mov x29, #33667
 cmp x30, x29
 b.ne irqprobe_bad
 umov x30, v29.d[1]
 mov x29, #33667
 cmp x30, x29
 b.ne irqprobe_bad
 umov x30, v30.d[0]
 mov x29, #33698
 cmp x30, x29
 b.ne irqprobe_bad
 umov x30, v30.d[1]
 mov x29, #33698
 cmp x30, x29
 b.ne irqprobe_bad
 umov x30, v31.d[0]
 mov x29, #33729
 cmp x30, x29
 b.ne irqprobe_bad
 umov x30, v31.d[1]
 mov x29, #33729
 cmp x30, x29
 b.ne irqprobe_bad
 mov x9, #42
 b irqprobe_restore
irqprobe_bad:
 mov x9, #17
irqprobe_restore:
 str x9, [sp, #TF_REG(0)]
 arch_restore_fp sp
 arch_restore_gprs sp
 add sp, sp, #HC_SIZE
 ret
.global arch_irqcheck
arch_irqcheck:
 mrs x9, sp_el0
 adrp x10, __emergency_top
 add x10, x10, :lo12:__emergency_top
 cmp x9, x10
 cset x0, eq
 ret
