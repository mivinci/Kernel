# This file is compiled into a flat binary and loaded from the disk image.
# It runs as PID 1 (init) in user mode.

  .text
  .globl _start
_start:
  /* SYS_WRITE: print start message */
  li    a7, 1           /* SYS_WRITE */
  li    a0, 1           /* stdout */
.La:  auipc a1, %pcrel_hi(init_msg)
  addi  a1, a1, %pcrel_lo(.La)
  li    a2, 18          /* strlen("init: starting sh\n") = 18 */
  ecall

  /* SYS_SPAWN: start the shell */
  li    a7, 8           /* SYS_SPAWN */
.Lb:  auipc a0, %pcrel_hi(shell_path)
  addi  a0, a0, %pcrel_lo(.Lb)
  ecall
  /* a0 = child pid (ignored for now) */

  /* Wait loop: reap child, then respawn */
1:
  li    a7, 9           /* SYS_WAIT */
  li    a0, -1          /* wait for any child */
  ecall
  /* a0 = child pid or -1 */
  /* Re-spawn the shell */
  li    a7, 8           /* SYS_SPAWN */
.Lc:  auipc a0, %pcrel_hi(shell_path)
  addi  a0, a0, %pcrel_lo(.Lc)
  ecall
  j     1b

  .balign 4
init_msg:
  .asciz "init: starting sh\n"

  .balign 4
shell_path:
  .asciz "/bin/sh"
