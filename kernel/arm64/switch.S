#include <arm64/context.h>
.section .text,"ax"
.global hal_ctx_switch
hal_ctx_switch:
 stp x19, x20, [x0, #KC_REG(0)]
 stp x21, x22, [x0, #KC_REG(2)]
 stp x23, x24, [x0, #KC_REG(4)]
 stp x25, x26, [x0, #KC_REG(6)]
 stp x27, x28, [x0, #KC_REG(8)]
 stp x29, x30, [x0, #KC_REG(10)]
 mov x9, sp
 str x9, [x0, #KC_REG(12)]
 str xzr, [x0, #KC_REG(13)]
 ldp x19, x20, [x1, #KC_REG(0)]
 ldp x21, x22, [x1, #KC_REG(2)]
 ldp x23, x24, [x1, #KC_REG(4)]
 ldp x25, x26, [x1, #KC_REG(6)]
 ldp x27, x28, [x1, #KC_REG(8)]
 ldp x29, x30, [x1, #KC_REG(10)]
 ldr x9, [x1, #KC_REG(12)]
 mov sp, x9
 ret
