#include <riscv/context.inc>
.section .text,"ax"
.global arch_irqprobe
arch_irqprobe:
.option norvc
.option norelax
 addi sp, sp, -HC_SIZE
 arch_save_gprs
 addi t0, sp, HC_SIZE
 sd t0, TF_SP(sp)
 arch_save_fp
 li x31, 32768
 fmv.d.x f0, x31
 li x31, 32799
 fmv.d.x f1, x31
 li x31, 32830
 fmv.d.x f2, x31
 li x31, 32861
 fmv.d.x f3, x31
 li x31, 32892
 fmv.d.x f4, x31
 li x31, 32923
 fmv.d.x f5, x31
 li x31, 32954
 fmv.d.x f6, x31
 li x31, 32985
 fmv.d.x f7, x31
 li x31, 33016
 fmv.d.x f8, x31
 li x31, 33047
 fmv.d.x f9, x31
 li x31, 33078
 fmv.d.x f10, x31
 li x31, 33109
 fmv.d.x f11, x31
 li x31, 33140
 fmv.d.x f12, x31
 li x31, 33171
 fmv.d.x f13, x31
 li x31, 33202
 fmv.d.x f14, x31
 li x31, 33233
 fmv.d.x f15, x31
 li x31, 33264
 fmv.d.x f16, x31
 li x31, 33295
 fmv.d.x f17, x31
 li x31, 33326
 fmv.d.x f18, x31
 li x31, 33357
 fmv.d.x f19, x31
 li x31, 33388
 fmv.d.x f20, x31
 li x31, 33419
 fmv.d.x f21, x31
 li x31, 33450
 fmv.d.x f22, x31
 li x31, 33481
 fmv.d.x f23, x31
 li x31, 33512
 fmv.d.x f24, x31
 li x31, 33543
 fmv.d.x f25, x31
 li x31, 33574
 fmv.d.x f26, x31
 li x31, 33605
 fmv.d.x f27, x31
 li x31, 33636
 fmv.d.x f28, x31
 li x31, 33667
 fmv.d.x f29, x31
 li x31, 33698
 fmv.d.x f30, x31
 li x31, 33729
 fmv.d.x f31, x31
 li x31, 33
 fscsr x31
 li x3, 4147
 li x4, 4164
 li x5, 4181
 li x6, 4198
 li x7, 4215
 li x8, 4232
 li x9, 4249
 li x10, 4266
 li x11, 4283
 li x12, 4300
 li x13, 4317
 li x14, 4334
 li x15, 4351
 li x16, 4368
 li x17, 4385
 li x18, 4402
 li x19, 4419
 li x20, 4436
 li x21, 4453
 li x22, 4470
 li x23, 4487
 li x24, 4504
 li x25, 4521
 li x26, 4538
 li x27, 4555
 li x28, 4572
 li x29, 4589
 li x30, 4606
 li x31, 4623
 call hal_idle
 li ra, 4147
 bne x3, ra, irqprobe_bad
 li ra, 4164
 bne x4, ra, irqprobe_bad
 li ra, 4181
 bne x5, ra, irqprobe_bad
 li ra, 4198
 bne x6, ra, irqprobe_bad
 li ra, 4215
 bne x7, ra, irqprobe_bad
 li ra, 4232
 bne x8, ra, irqprobe_bad
 li ra, 4249
 bne x9, ra, irqprobe_bad
 li ra, 4266
 bne x10, ra, irqprobe_bad
 li ra, 4283
 bne x11, ra, irqprobe_bad
 li ra, 4300
 bne x12, ra, irqprobe_bad
 li ra, 4317
 bne x13, ra, irqprobe_bad
 li ra, 4334
 bne x14, ra, irqprobe_bad
 li ra, 4351
 bne x15, ra, irqprobe_bad
 li ra, 4368
 bne x16, ra, irqprobe_bad
 li ra, 4385
 bne x17, ra, irqprobe_bad
 li ra, 4402
 bne x18, ra, irqprobe_bad
 li ra, 4419
 bne x19, ra, irqprobe_bad
 li ra, 4436
 bne x20, ra, irqprobe_bad
 li ra, 4453
 bne x21, ra, irqprobe_bad
 li ra, 4470
 bne x22, ra, irqprobe_bad
 li ra, 4487
 bne x23, ra, irqprobe_bad
 li ra, 4504
 bne x24, ra, irqprobe_bad
 li ra, 4521
 bne x25, ra, irqprobe_bad
 li ra, 4538
 bne x26, ra, irqprobe_bad
 li ra, 4555
 bne x27, ra, irqprobe_bad
 li ra, 4572
 bne x28, ra, irqprobe_bad
 li ra, 4589
 bne x29, ra, irqprobe_bad
 li ra, 4606
 bne x30, ra, irqprobe_bad
 li ra, 4623
 bne x31, ra, irqprobe_bad
 frcsr t0
 li ra, 33
 bne t0, ra, irqprobe_bad
 fmv.x.d t0, f0
 li ra, 32768
 bne t0, ra, irqprobe_bad
 fmv.x.d t0, f1
 li ra, 32799
 bne t0, ra, irqprobe_bad
 fmv.x.d t0, f2
 li ra, 32830
 bne t0, ra, irqprobe_bad
 fmv.x.d t0, f3
 li ra, 32861
 bne t0, ra, irqprobe_bad
 fmv.x.d t0, f4
 li ra, 32892
 bne t0, ra, irqprobe_bad
 fmv.x.d t0, f5
 li ra, 32923
 bne t0, ra, irqprobe_bad
 fmv.x.d t0, f6
 li ra, 32954
 bne t0, ra, irqprobe_bad
 fmv.x.d t0, f7
 li ra, 32985
 bne t0, ra, irqprobe_bad
 fmv.x.d t0, f8
 li ra, 33016
 bne t0, ra, irqprobe_bad
 fmv.x.d t0, f9
 li ra, 33047
 bne t0, ra, irqprobe_bad
 fmv.x.d t0, f10
 li ra, 33078
 bne t0, ra, irqprobe_bad
 fmv.x.d t0, f11
 li ra, 33109
 bne t0, ra, irqprobe_bad
 fmv.x.d t0, f12
 li ra, 33140
 bne t0, ra, irqprobe_bad
 fmv.x.d t0, f13
 li ra, 33171
 bne t0, ra, irqprobe_bad
 fmv.x.d t0, f14
 li ra, 33202
 bne t0, ra, irqprobe_bad
 fmv.x.d t0, f15
 li ra, 33233
 bne t0, ra, irqprobe_bad
 fmv.x.d t0, f16
 li ra, 33264
 bne t0, ra, irqprobe_bad
 fmv.x.d t0, f17
 li ra, 33295
 bne t0, ra, irqprobe_bad
 fmv.x.d t0, f18
 li ra, 33326
 bne t0, ra, irqprobe_bad
 fmv.x.d t0, f19
 li ra, 33357
 bne t0, ra, irqprobe_bad
 fmv.x.d t0, f20
 li ra, 33388
 bne t0, ra, irqprobe_bad
 fmv.x.d t0, f21
 li ra, 33419
 bne t0, ra, irqprobe_bad
 fmv.x.d t0, f22
 li ra, 33450
 bne t0, ra, irqprobe_bad
 fmv.x.d t0, f23
 li ra, 33481
 bne t0, ra, irqprobe_bad
 fmv.x.d t0, f24
 li ra, 33512
 bne t0, ra, irqprobe_bad
 fmv.x.d t0, f25
 li ra, 33543
 bne t0, ra, irqprobe_bad
 fmv.x.d t0, f26
 li ra, 33574
 bne t0, ra, irqprobe_bad
 fmv.x.d t0, f27
 li ra, 33605
 bne t0, ra, irqprobe_bad
 fmv.x.d t0, f28
 li ra, 33636
 bne t0, ra, irqprobe_bad
 fmv.x.d t0, f29
 li ra, 33667
 bne t0, ra, irqprobe_bad
 fmv.x.d t0, f30
 li ra, 33698
 bne t0, ra, irqprobe_bad
 fmv.x.d t0, f31
 li ra, 33729
 bne t0, ra, irqprobe_bad
 li t0, 42
 j irqprobe_restore
irqprobe_bad:
 li t0, 17
irqprobe_restore:
 sd t0, TF_REG(10)(sp)
 arch_restore_fp sp
 arch_restore_gprs sp
 ret
.global arch_irqcheck
arch_irqcheck:
 csrr t0, sscratch
 la t1, __emergency_top
 xor a0, t0, t1
 seqz a0, a0
 ret
