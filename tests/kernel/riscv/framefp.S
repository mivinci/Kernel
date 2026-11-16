.section .text
.option norvc
.balign 4
.global frame_fp
.type frame_fp,@function
frame_fp:
 fsrm a0,a0
 ret
.size frame_fp,.-frame_fp
.section .note.GNU-stack,"",@progbits
