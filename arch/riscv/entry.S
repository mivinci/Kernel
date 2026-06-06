  .text
  .globl  _start
  .extern main
_start:
  /*
   Choose a hart to boot to boot the kernel
   */
  la       a3, lottery
  li       a2, 1
  amoadd.w a3, a2, (a3)
  bnez     a3, wait

  /*
   * Set up stack for the lucky hart.
   * Stack grows downward from stack0 + 16384.
   *  - 16 KB is generous for early kernel init without heap
   *  - .align 12 (4 KB) ensures bottom of stack is page-aligned,
   *    and sp = stack0 + 16384 is 16-byte aligned (RISC-V ABI)
   */
  la       a3, stack0
  li       a4, 16384
  add      sp, a3, a4

  /*
   Jump to C
   */
  csrr     a0,    mhartid
  call     main

wait:
  wfi
  j        wait

  .bss
  .align 12           /* 4 KB alignment for page tables / guard page */
lottery:
  .word  0
stack0:
  .skip 16384         /* 16 KB bootstrap stack */
