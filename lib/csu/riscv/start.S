.section .text._start
.balign 4
.global _start
.type _start, @function
_start:
    li ra, 0
    tail csustart
.size _start, .-_start
.section .note.GNU-stack,"",@progbits
