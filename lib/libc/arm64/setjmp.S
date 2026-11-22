.arch_extension fp
.section .text.setjmp
.balign 4
.global setjmp
.type setjmp, %function
setjmp:
    stp x19, x20, [x0, #0]
    stp x21, x22, [x0, #16]
    stp x23, x24, [x0, #32]
    stp x25, x26, [x0, #48]
    stp x27, x28, [x0, #64]
    stp x29, x30, [x0, #80]
    mov x2, sp
    str x2, [x0, #96]
    stp d8, d9, [x0, #104]
    stp d10, d11, [x0, #120]
    stp d12, d13, [x0, #136]
    stp d14, d15, [x0, #152]
    adrp x2, __libc_signal_depth
    ldr w2, [x2, :lo12:__libc_signal_depth]
    str w2, [x0, #168]
    mov w0, wzr
    ret
.size setjmp, .-setjmp

.section .text.longjmp
.balign 4
.global longjmp
.type longjmp, %function
longjmp:
    adrp x2, __libc_signal_depth
    ldr w2, [x2, :lo12:__libc_signal_depth]
    ldr w3, [x0, #168]
    cmp w2, w3
    b.ne abort
    ldp x19, x20, [x0, #0]
    ldp x21, x22, [x0, #16]
    ldp x23, x24, [x0, #32]
    ldp x25, x26, [x0, #48]
    ldp x27, x28, [x0, #64]
    ldp x29, x30, [x0, #80]
    ldr x2, [x0, #96]
    ldp d8, d9, [x0, #104]
    ldp d10, d11, [x0, #120]
    ldp d12, d13, [x0, #136]
    ldp d14, d15, [x0, #152]
    mov sp, x2
    cmp w1, #0
    csinc w0, w1, wzr, ne
    ret
.size longjmp, .-longjmp
.section .note.GNU-stack,"",%progbits
