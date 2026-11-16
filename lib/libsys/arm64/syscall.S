.section .text
.balign 4
.global __sys_enter
.type __sys_enter, %function
__sys_enter:
    stp x19, x30, [sp, #-16]!
    mov x19, x2
    mov x10, x1
    mov x8, x0
    ldp x0, x1, [x10]
    ldp x2, x3, [x10, #16]
    ldp x4, x5, [x10, #32]
    ldr x6, [x10, #48]
    mov x7, xzr
    svc #0
    stp x1, x2, [x19]
    stp x3, x4, [x19, #16]
    stp x5, x6, [x19, #32]
    str x7, [x19, #48]
    str x9, [x19, #56]
    ldp x19, x30, [sp], #16
    ret
.size __sys_enter, .-__sys_enter
.section .note.GNU-stack,"",%progbits
