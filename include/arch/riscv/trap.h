#ifndef _ARCH_RISCV_TRAP_H
#define _ARCH_RISCV_TRAP_H

#include <libc.h>

/* Exception cause codes (mcause) */
#define MCAUSE_ECALL_U  8  /* Environment call from U-mode */
#define MCAUSE_ECALL_S  9  /* Environment call from S-mode */
#define MCAUSE_ECALL_M  11 /* Environment call from M-mode */
#define MCAUSE_INST_PF  12 /* Instruction page fault */
#define MCAUSE_LOAD_PF  13 /* Load page fault */
#define MCAUSE_STORE_PF 15 /* Store page fault */

/* Interrupt cause codes (mcause high bit set) */
#define MCAUSE_MTIMER 7  /* Machine timer interrupt */
#define MCAUSE_MEXT   11 /* Machine external interrupt */
#define MCAUSE_MSI    3  /* Machine software interrupt */

/* mstatus bits */
#define MSTATUS_MIE   (1UL << 3)  /* Machine interrupt enable */
#define MSTATUS_MPIE  (1UL << 7)  /* Machine previous interrupt enable */
#define MSTATUS_MPP   (3UL << 11) /* Machine previous privilege */
#define MSTATUS_MPP_M (3UL << 11) /* M-mode previous */

/*
 * Trap frame saved on stack by trap_entry.
 * Layout must match trap.S save order.
 */
XDEF_STRUCT(TrapFrame) {
  unsigned long ra;
  unsigned long sp;
  unsigned long gp;
  unsigned long tp;
  unsigned long t0;
  unsigned long t1;
  unsigned long t2;
  unsigned long s0;
  unsigned long s1;
  unsigned long a0;
  unsigned long a1;
  unsigned long a2;
  unsigned long a3;
  unsigned long a4;
  unsigned long a5;
  unsigned long a6;
  unsigned long a7;
  unsigned long s2;
  unsigned long s3;
  unsigned long s4;
  unsigned long s5;
  unsigned long s6;
  unsigned long s7;
  unsigned long s8;
  unsigned long s9;
  unsigned long s10;
  unsigned long s11;
  unsigned long t3;
  unsigned long t4;
  unsigned long t5;
  unsigned long t6;
  /* CSR values */
  unsigned long mepc;
  unsigned long mstatus;
  unsigned long mcause;
  unsigned long mtval;
};

void trap_init(void);
void trap_handler(TrapFrame *tf);

#endif /* _ARCH_RISCV_TRAP_H */
