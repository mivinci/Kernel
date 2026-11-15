#include <arm64/context.h>
.section .text,"ax"
.global arch_switchprobe
arch_switchprobe:
 sub sp, sp, #KC_SIZE
 stp x19, x20, [sp, #KC_REG(0)]
 stp x21, x22, [sp, #KC_REG(2)]
 stp x23, x24, [sp, #KC_REG(4)]
 stp x25, x26, [sp, #KC_REG(6)]
 stp x27, x28, [sp, #KC_REG(8)]
 stp x29, x30, [sp, #KC_REG(10)]
 mov x19, #512
 mov x20, #513
 mov x21, #514
 mov x22, #515
 mov x23, #516
 mov x24, #517
 mov x25, #518
 mov x26, #519
 mov x27, #520
 mov x28, #521
 mov x29, #522
 bl hal_ctx_switch
 cmp x19, #512
 b.ne 1f
 cmp x20, #513
 b.ne 1f
 cmp x21, #514
 b.ne 1f
 cmp x22, #515
 b.ne 1f
 cmp x23, #516
 b.ne 1f
 cmp x24, #517
 b.ne 1f
 cmp x25, #518
 b.ne 1f
 cmp x26, #519
 b.ne 1f
 cmp x27, #520
 b.ne 1f
 cmp x28, #521
 b.ne 1f
 cmp x29, #522
 b.ne 1f
 mov x0, #1
 b 2f
1: mov x0, #0
2:
 ldp x19, x20, [sp, #KC_REG(0)]
 ldp x21, x22, [sp, #KC_REG(2)]
 ldp x23, x24, [sp, #KC_REG(4)]
 ldp x25, x26, [sp, #KC_REG(6)]
 ldp x27, x28, [sp, #KC_REG(8)]
 ldp x29, x30, [sp, #KC_REG(10)]
 add sp, sp, #KC_SIZE
 ret
.global arch_switchinit
arch_switchinit:
 str x2, [x0, #KC_REG(11)]
 str x1, [x0, #KC_REG(12)]
 ret
.global arch_stackpointer
arch_stackpointer:
 mov x0, sp
 ret
