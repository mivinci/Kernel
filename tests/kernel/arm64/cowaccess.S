.arch armv8-a+fp+simd
.section .text
.balign 4
.global cow_store,cow_store_fault
.type cow_store,%function
cow_store:
 movz x2,#0x3ff0,lsl #48
 fmov d0,x2
 mov x9,#0x11
 mov x10,#0x22
 mov x11,#0x33
 mov x12,#0x44
 mov x13,#0x55
 mov x14,#0x66
 mov x15,#0x77
 mov x16,#0x88
 mov x17,#0x99
 cmp x1,x1
cow_store_fault:
 str x1,[x0]
 b.ne 1f
 ldr x2,[x0]
 cmp x2,x1
 b.ne 1f
 cmp x9,#0x11
 b.ne 1f
 cmp x10,#0x22
 b.ne 1f
 cmp x11,#0x33
 b.ne 1f
 cmp x12,#0x44
 b.ne 1f
 cmp x13,#0x55
 b.ne 1f
 cmp x14,#0x66
 b.ne 1f
 cmp x15,#0x77
 b.ne 1f
 cmp x16,#0x88
 b.ne 1f
 cmp x17,#0x99
 b.ne 1f
 fmov x2,d0
 movz x3,#0x3ff0,lsl #48
 cmp x2,x3
 b.ne 1f
 mov x0,#1
 ret
1:
 mov x0,#0
 ret
.size cow_store,.-cow_store
.section .note.GNU-stack,"",%progbits
