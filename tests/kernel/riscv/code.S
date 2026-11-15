.section .rodata
.balign 4
.global code_begin
.global code_end
code_begin:
  .option norvc
  li a0, 42
  ret
code_end:
