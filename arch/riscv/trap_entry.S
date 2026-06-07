#include <arch/riscv/arch.h>

  .text
  .globl  trap_entry
  .extern trap_handler

trap_entry:
  /*
   * Swap sp with kernel trap stack (stored in mscratch).
   * After swap: sp = kernel stack top, mscratch = original sp.
   */
  csrrw   sp, mscratch, sp

#if XLEN == 64
#define FRAME_SIZE 288
#define STORE       sd
#define LOAD        ld
#else
#define FRAME_SIZE 144
#define STORE       sw
#define LOAD        lw
#endif

  addi    sp, sp, -FRAME_SIZE

  STORE   ra,  0(sp)
  addi    t0, sp, FRAME_SIZE
  STORE   t0,  1*REGSZ(sp)
  STORE   gp,  2*REGSZ(sp)
  STORE   tp,  3*REGSZ(sp)
  STORE   t0,  4*REGSZ(sp)    /* original t0 saved above */
  STORE   t1,  5*REGSZ(sp)
  STORE   t2,  6*REGSZ(sp)
  STORE   s0,  7*REGSZ(sp)
  STORE   s1,  8*REGSZ(sp)
  STORE   a0,  9*REGSZ(sp)
  STORE   a1, 10*REGSZ(sp)
  STORE   a2, 11*REGSZ(sp)
  STORE   a3, 12*REGSZ(sp)
  STORE   a4, 13*REGSZ(sp)
  STORE   a5, 14*REGSZ(sp)
  STORE   a6, 15*REGSZ(sp)
  STORE   a7, 16*REGSZ(sp)
  STORE   s2, 17*REGSZ(sp)
  STORE   s3, 18*REGSZ(sp)
  STORE   s4, 19*REGSZ(sp)
  STORE   s5, 20*REGSZ(sp)
  STORE   s6, 21*REGSZ(sp)
  STORE   s7, 22*REGSZ(sp)
  STORE   s8, 23*REGSZ(sp)
  STORE   s9, 24*REGSZ(sp)
  STORE   s10,25*REGSZ(sp)
  STORE   s11,26*REGSZ(sp)
  STORE   t3, 27*REGSZ(sp)
  STORE   t4, 28*REGSZ(sp)
  STORE   t5, 29*REGSZ(sp)
  STORE   t6, 30*REGSZ(sp)

  /* Save CSRs at offsets 31..34 */
  csrr    t0, mepc
  STORE   t0, 31*REGSZ(sp)
  csrr    t0, mstatus
  STORE   t0, 32*REGSZ(sp)
  csrr    t0, mcause
  STORE   t0, 33*REGSZ(sp)
  csrr    t0, mtval
  STORE   t0, 34*REGSZ(sp)

  /* Call C handler */
  mv      a0, sp
  call    trap_handler

  /* Restore CSRs */
  LOAD    t0, 32*REGSZ(sp)
  csrw    mstatus, t0
  LOAD    t0, 33*REGSZ(sp)
  csrw    mcause,  t0
  LOAD    t0, 34*REGSZ(sp)
  csrw    mtval,   t0
  LOAD    t0, 31*REGSZ(sp)
  csrw    mepc,    t0

  /* Restore general registers */
  LOAD    ra,  0(sp)
  LOAD    gp,  2*REGSZ(sp)
  LOAD    tp,  3*REGSZ(sp)
  LOAD    t0,  4*REGSZ(sp)
  LOAD    t1,  5*REGSZ(sp)
  LOAD    t2,  6*REGSZ(sp)
  LOAD    s0,  7*REGSZ(sp)
  LOAD    s1,  8*REGSZ(sp)
  LOAD    a0,  9*REGSZ(sp)
  LOAD    a1, 10*REGSZ(sp)
  LOAD    a2, 11*REGSZ(sp)
  LOAD    a3, 12*REGSZ(sp)
  LOAD    a4, 13*REGSZ(sp)
  LOAD    a5, 14*REGSZ(sp)
  LOAD    a6, 15*REGSZ(sp)
  LOAD    a7, 16*REGSZ(sp)
  LOAD    s2, 17*REGSZ(sp)
  LOAD    s3, 18*REGSZ(sp)
  LOAD    s4, 19*REGSZ(sp)
  LOAD    s5, 20*REGSZ(sp)
  LOAD    s6, 21*REGSZ(sp)
  LOAD    s7, 22*REGSZ(sp)
  LOAD    s8, 23*REGSZ(sp)
  LOAD    s9, 24*REGSZ(sp)
  LOAD    s10,25*REGSZ(sp)
  LOAD    s11,26*REGSZ(sp)
  LOAD    t3, 27*REGSZ(sp)
  LOAD    t4, 28*REGSZ(sp)
  LOAD    t5, 29*REGSZ(sp)
  LOAD    t6, 30*REGSZ(sp)

  addi    sp, sp, FRAME_SIZE

  /* Restore original sp (saved in mscratch on entry) */
  csrrw   sp, mscratch, sp

  mret
