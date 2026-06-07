# Multi-hart bootstrap.  Every hart sets up its own stack.
# Hart 0 boots the kernel.  Others wait for hart_ready, then
# enter the scheduler with their hartid.

  .text
  .globl  _start
  .extern kmain
  .extern scheduler

_start:
  /* Per-hart stack: sp = stack0 + hartid * 16384 + 16384 */
  mv       s0, a0               /* s0 = hartid */
  mv       s1, a1               /* s1 = FDT pointer */
  la       a3, stack0
  li       a4, 16384
  mul      a5, a4, s0
  add      a3, a3, a5
  add      sp, a3, a4

  beqz     s0, boot_kernel

  /* Non-zero harts: wait for hart 0 to finish init */
  la       a3, hart_ready
1:
  lw       a4, 0(a3)
  beqz     a4, 1b
  /* Enter scheduler with hartid in a0 */
  mv       a0, s0
  call     scheduler
  wfi
  j        1b

boot_kernel:
  mv       a0, s0
  mv       a1, s1
  call     kmain

  .bss
  .align 12
  .globl  hart_ready
hart_ready:
  .word  0
stack0:
  .skip 16384 * NSTACK
