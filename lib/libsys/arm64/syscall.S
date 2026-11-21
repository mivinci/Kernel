#include <abi/base.h>
#include <abi/syscall.h>
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

.global __sys_forkprep
.type __sys_forkprep, %function
__sys_forkprep:
    cbz x2, 1f
    str x19, [sp, #-16]!
    mov x19, x2
    mov x2, xzr
    mov x3, xzr
    mov x4, xzr
    mov x5, xzr
    mov x6, xzr
    mov x7, xzr
    mov x8, #SYS_FORKPREP
    svc #0
.global __sys_forkreturn_begin
__sys_forkreturn_begin:
    stp x1, x2, [x19]
    str x3, [x19, #16]
.global __sys_forkreturn_end
__sys_forkreturn_end:
    ldr x19, [sp], #16
    ret
1:
    mov x0, #-ERR_INVALID
    ret
.size __sys_forkprep, .-__sys_forkprep
.section .note.GNU-stack,"",%progbits
