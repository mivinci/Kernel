#include <arm64/trap.h>
.section .text.boot,"ax"
.global _start
_start:
 msr daifset, #15
 mrs x1, CurrentEL
 cmp x1, #4
 b.ne hal_halt
 mrs x1, mpidr_el1
 and x1, x1, #0xffffff
 cbnz x1, hal_halt
 msr spsel, #1
 adr x1, earlyvectors
 msr vbar_el1, x1
 isb
 ldr x1, =__emergency_top
 msr sp_el0, x1
 ldr x1, =__stack_top
 mov sp, x1
 ldr x1, =__bss_start
 ldr x2, =__bss_end
1: cmp x1, x2
 b.hs 2f
 str xzr, [x1], #8
 b 1b
2: bl arch_start
 b hal_halt
.balign 2048
earlyvectors:
.rept 16
 b hal_halt
 .balign 128
.endr
.balign 2048
.global vectors
vectors:
.rept 16
 b trapentry
 .balign 128
.endr
trapentry:
 // Change stack selection without consuming an interrupted general register.
 msr spsel, #0
 sub sp, sp, #TF_SIZE
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
 msr spsel, #1
 mov x9, sp
 msr spsel, #0
 str x9, [sp, #TF_SP]
 mrs x9, elr_el1
 str x9, [sp, #TF_PC]
 mrs x9, spsr_el1
 str x9, [sp, #TF_STATUS]
 mrs x9, esr_el1
 str x9, [sp, #TF_CAUSE]
 mrs x9, far_el1
 str x9, [sp, #TF_ADDRESS]
 adr x9, earlyvectors
 msr vbar_el1, x9
 isb
 mov x0, sp
 bl arch_trap
 adr x9, vectors
 msr vbar_el1, x9
 isb
 ldr x9, [sp, #TF_PC]
 msr elr_el1, x9
 ldr x9, [sp, #TF_STATUS]
 msr spsr_el1, x9
 ldp x0, x1, [sp, #TF_REG(0)]
 ldp x2, x3, [sp, #TF_REG(2)]
 ldp x4, x5, [sp, #TF_REG(4)]
 ldp x6, x7, [sp, #TF_REG(6)]
 ldp x8, x9, [sp, #TF_REG(8)]
 ldp x10, x11, [sp, #TF_REG(10)]
 ldp x12, x13, [sp, #TF_REG(12)]
 ldp x14, x15, [sp, #TF_REG(14)]
 ldp x16, x17, [sp, #TF_REG(16)]
 ldp x18, x19, [sp, #TF_REG(18)]
 ldp x20, x21, [sp, #TF_REG(20)]
 ldp x22, x23, [sp, #TF_REG(22)]
 ldp x24, x25, [sp, #TF_REG(24)]
 ldp x26, x27, [sp, #TF_REG(26)]
 ldp x28, x29, [sp, #TF_REG(28)]
 ldr x30, [sp, #TF_REG(30)]
 add sp, sp, #TF_SIZE
 msr spsel, #1
 eret
.global hal_halt
hal_halt:
 msr daifset, #15
1: wfi
 b 1b
