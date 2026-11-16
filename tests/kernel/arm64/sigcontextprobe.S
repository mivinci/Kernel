.arch armv8-a+fp+simd
.section .rodata
.balign 8
.global proc_begin,proc_end,proc_call
proc_begin:
 mov x15,#1
 mrs x12,nzcv
 mov x13,#0xa0000000
 cmp x12,x13
 b.ne 1f
 mov x15,#2
 mov x9,#0x3fff000000
 cmp x0,x9
 b.ne 1f
 ldr x10,[x0]
 ldr x11,2f
 cmp x10,x11
 b.ne 1f
 mov x15,#3
 mov x9,#0x5678
 umov x10,v31.d[0]
 cmp x10,x9
 b.ne 1f
 umov x10,v31.d[1]
 cmp x10,x9
 b.ne 1f
 mov x15,#4
 movz x9,#0x5678
 movk x9,#0x1234,lsl #16
 cmp x19,x9
 b.ne 1f
 mov x15,#5
 mrs x9,fpcr
 cmp x9,x6
 b.ne 1f
 mov x15,#6
 mrs x9,fpsr
 cmp x9,x7
 b.ne 1f
 mov x0,#42
 mov x1,xzr
 mov x2,xzr
 mov x3,xzr
 mov x4,xzr
 mov x5,xzr
 mov x6,xzr
 mov x7,xzr
 mov x8,#1
proc_call:
 svc #0
1:
 add x0,x15,#180
 mov x1,xzr
 mov x2,xzr
 mov x3,xzr
 mov x4,xzr
 mov x5,xzr
 mov x6,xzr
 mov x7,xzr
 mov x8,#1
 svc #0
 .word 0
.balign 8
2: .quad 0x4b424f4f54303031
proc_end:
