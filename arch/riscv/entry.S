  .text
  .globl  _start
  .extern kmain
_start:
  la       a3, lottery
  li       a2, 1
  amoadd.w a3, a2, (a3)
  bnez     a3, wait

  mv       s0, a0           /* s0 = hartid */
  mv       s1, a1           /* s1 = FDT pointer */
  la       a3, stack0
  li       a4, 16384
  mul      a5, a4, s0
  add      a3, a3, a5
  add      sp, a3, a4

  mv       a0, s0
  mv       a1, s1
  call     kmain

wait:
  wfi
  j        wait

  .bss
  .align 12
lottery:
  .word  0
stack0:
  .skip 16384 * NSTACK
