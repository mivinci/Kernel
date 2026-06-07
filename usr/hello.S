# This file is compiled into a flat binary and embedded in the kernel.
# It runs in user mode at address 0x0.

  .text
  .globl _start
_start:
  /* SYS_WRITE: print a message */
  li    a7, 1           /* SYS_WRITE */
  li    a0, 1           /* stdout */
  auipc a1, 0
  addi  a1, a1, 20      /* offset to msg (0x18 - 0x4 = 20) */
  li    a2, 20          /* length */
  ecall

  /* Loop: SYS_YIELD forever */
1:
  li    a7, 3           /* SYS_YIELD */
  ecall
  j     1b

  /* Embedded message (null-terminated) */
msg:
  .ascii "Hello from ramfs!\n"
  .byte 0
