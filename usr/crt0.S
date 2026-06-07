# This file is compiled into a flat binary and loaded by the kernel.
# It sets up a stack and jumps to main().

# Flat-binary startup.  Sets up a user stack, calls main(), exits on return.

  .text
  .globl _start
_start:
  la    sp, _stack_top
  call  main
  /* Pass main() return value (a0) as exit code */
  li    a7, 2    /* SYS_EXIT */
  ecall
  j     _start

  .bss
  .balign 16
  .space 512    /* 512-byte user stack — keep total < PAGE_SIZE */
_stack_top:
