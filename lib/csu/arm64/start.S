.section .text._start
.balign 4
.global _start
.type _start, %function
_start:
    mov x29, xzr
    mov x30, xzr
    b csustart
.size _start, .-_start
.section .note.GNU-stack,"",%progbits
