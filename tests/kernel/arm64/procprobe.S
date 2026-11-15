.section .rodata
.balign 8
.global proc_begin, proc_end, proc_call
proc_begin:
 mov x9, #0x3fff000000
 cmp x0, x9
 b.ne 1f
 ldr x10, [x0]
 ldr x11, 2f
 cmp x10, x11
 b.ne 1f
 mov x0, #42
 mov x8, #1
proc_call:
 svc #0
1: .word 0
.balign 8
2: .quad 0x4b424f4f54303031
proc_end:
