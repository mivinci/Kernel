#include <abi/syscall.h>
.section .rodata
.balign 8
.global timer_begin, timer_end
timer_begin:
.option norvc
 li x5, 0x40000
 li x7, 0
 li x1, 4113
 li x3, 4147
 li x4, 4164
 li x6, 4198
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
 li x29, 4589
 li x30, 4606
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
timer_loop:
 ld x28, 0(x5)
 addi x7, x7, 1
 sd x7, 8(x5)
 beqz x28, timer_loop
 beqz x7, timer_bad
 li x28, 33
 bne x31, x28, timer_bad
 li x31, 4113
 bne x1, x31, timer_bad
 li x31, 4147
 bne x3, x31, timer_bad
 li x31, 4164
 bne x4, x31, timer_bad
 li x31, 4198
 bne x6, x31, timer_bad
 li x31, 4232
 bne x8, x31, timer_bad
 li x31, 4249
 bne x9, x31, timer_bad
 li x31, 4266
 bne x10, x31, timer_bad
 li x31, 4283
 bne x11, x31, timer_bad
 li x31, 4300
 bne x12, x31, timer_bad
 li x31, 4317
 bne x13, x31, timer_bad
 li x31, 4334
 bne x14, x31, timer_bad
 li x31, 4351
 bne x15, x31, timer_bad
 li x31, 4368
 bne x16, x31, timer_bad
 li x31, 4385
 bne x17, x31, timer_bad
 li x31, 4402
 bne x18, x31, timer_bad
 li x31, 4419
 bne x19, x31, timer_bad
 li x31, 4436
 bne x20, x31, timer_bad
 li x31, 4453
 bne x21, x31, timer_bad
 li x31, 4470
 bne x22, x31, timer_bad
 li x31, 4487
 bne x23, x31, timer_bad
 li x31, 4504
 bne x24, x31, timer_bad
 li x31, 4521
 bne x25, x31, timer_bad
 li x31, 4538
 bne x26, x31, timer_bad
 li x31, 4555
 bne x27, x31, timer_bad
 li x31, 4589
 bne x29, x31, timer_bad
 li x31, 4606
 bne x30, x31, timer_bad
 li x31, 0x3fffff0000
 bne sp, x31, timer_bad
 frcsr x30
 li x31, 33
 bne x30, x31, timer_bad
 fmv.x.d x30, f0
 li x31, 32768
 bne x30, x31, timer_bad
 fmv.x.d x30, f1
 li x31, 32799
 bne x30, x31, timer_bad
 fmv.x.d x30, f2
 li x31, 32830
 bne x30, x31, timer_bad
 fmv.x.d x30, f3
 li x31, 32861
 bne x30, x31, timer_bad
 fmv.x.d x30, f4
 li x31, 32892
 bne x30, x31, timer_bad
 fmv.x.d x30, f5
 li x31, 32923
 bne x30, x31, timer_bad
 fmv.x.d x30, f6
 li x31, 32954
 bne x30, x31, timer_bad
 fmv.x.d x30, f7
 li x31, 32985
 bne x30, x31, timer_bad
 fmv.x.d x30, f8
 li x31, 33016
 bne x30, x31, timer_bad
 fmv.x.d x30, f9
 li x31, 33047
 bne x30, x31, timer_bad
 fmv.x.d x30, f10
 li x31, 33078
 bne x30, x31, timer_bad
 fmv.x.d x30, f11
 li x31, 33109
 bne x30, x31, timer_bad
 fmv.x.d x30, f12
 li x31, 33140
 bne x30, x31, timer_bad
 fmv.x.d x30, f13
 li x31, 33171
 bne x30, x31, timer_bad
 fmv.x.d x30, f14
 li x31, 33202
 bne x30, x31, timer_bad
 fmv.x.d x30, f15
 li x31, 33233
 bne x30, x31, timer_bad
 fmv.x.d x30, f16
 li x31, 33264
 bne x30, x31, timer_bad
 fmv.x.d x30, f17
 li x31, 33295
 bne x30, x31, timer_bad
 fmv.x.d x30, f18
 li x31, 33326
 bne x30, x31, timer_bad
 fmv.x.d x30, f19
 li x31, 33357
 bne x30, x31, timer_bad
 fmv.x.d x30, f20
 li x31, 33388
 bne x30, x31, timer_bad
 fmv.x.d x30, f21
 li x31, 33419
 bne x30, x31, timer_bad
 fmv.x.d x30, f22
 li x31, 33450
 bne x30, x31, timer_bad
 fmv.x.d x30, f23
 li x31, 33481
 bne x30, x31, timer_bad
 fmv.x.d x30, f24
 li x31, 33512
 bne x30, x31, timer_bad
 fmv.x.d x30, f25
 li x31, 33543
 bne x30, x31, timer_bad
 fmv.x.d x30, f26
 li x31, 33574
 bne x30, x31, timer_bad
 fmv.x.d x30, f27
 li x31, 33605
 bne x30, x31, timer_bad
 fmv.x.d x30, f28
 li x31, 33636
 bne x30, x31, timer_bad
 fmv.x.d x30, f29
 li x31, 33667
 bne x30, x31, timer_bad
 fmv.x.d x30, f30
 li x31, 33698
 bne x30, x31, timer_bad
 fmv.x.d x30, f31
 li x31, 33729
 bne x30, x31, timer_bad
 li a0, 42
 j timer_exit
timer_bad:
 li a0, 17
timer_exit:
 li a1, 0
 li a2, 0
 li a3, 0
 li a4, 0
 li a5, 0
 li a6, 0
 li a7, SYS_EXIT
 ecall
 j timer_bad
timer_end:
