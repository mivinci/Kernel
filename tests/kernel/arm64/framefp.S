.arch armv8-a+fp+simd
.section .text
.balign 4
.global frame_fp
.type frame_fp,%function
frame_fp:
 mrs x9,fpcr
 ubfx x10,x9,#22,#2
 bfi x9,x0,#22,#2
 msr fpcr,x9
 mov x0,x10
 ret
.size frame_fp,.-frame_fp
.section .note.GNU-stack,"",%progbits
