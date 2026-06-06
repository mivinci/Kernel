  .text
  .globl  _start
  .extern main
_start:
  /*
   * Choose a hart to boot the kernel via atomic lottery.
   * Only the winning hart continues; losers spin in wfi.
   */
  la       a3, lottery
  li       a2, 1
  amoadd.w a3, a2, (a3)
  bnez     a3, wait

  /*
   * Set up per-hart stack. Stacks are contiguous in .bss,
   * each 16 KB (1 << 14). The winning hart computes:
   *   sp = stack0 + hartid * 16384 + 16384
   * NSTACK is defined via -D from the Makefile (matches SMP).
   */
  csrr     a0, mhartid
  la       a3, stack0
  li       a4, 16384
  mul      a5, a4, a0
  add      a3, a3, a5
  add      sp, a3, a4

  /*
   * Jump to C with hartid in a0.
   */
  call     main

wait:
  wfi
  j        wait

  .bss
  .align 12           /* 4 KB alignment for page tables / guard page */
lottery:
  .word  0
stack0:
  .skip 16384 * NSTACK  /* Total: NSTACK × 16 KB bootstrap stacks */
