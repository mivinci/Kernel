#include <arch/riscv/arch.h>
#include <arch/riscv/csr.h>
#include <arch/riscv/plic.h>
#include <arch/riscv/timer.h>
#include <arch/riscv/trap.h>
#include <kernel.h>
#include <syscall.h>

extern void trap_entry(void);

void trap_init(void) {
  csr_write(mtvec, (unsigned long)trap_entry);
  csr_write(mstatus, csr_read(mstatus) | MSTATUS_MIE);

  /*
   * Each process sets mscratch to its kstack top in proc_create.
   * kernel threads (M-mode) and user processes (U-mode) both use
   * their process kernel stack for trap frames.
   */
  printk("[trap] mtvec=%p mstatus=%p\n", trap_entry, csr_read(mstatus));
}

void trap_handler(TrapFrame *tf) {
  unsigned long cause   = tf->mcause;
  unsigned long is_intr = cause >> (XLEN - 1);

  if (is_intr) {
    cause &= ~(1UL << (XLEN - 1));
    switch (cause) {
    case MCAUSE_MTIMER: timer_handle(); break;
    case MCAUSE_MEXT:   plic_handle(); break;
    case MCAUSE_MSI:
      /* TODO: software interrupt handler (Phase 4) */
      break;
    default: printk("[trap] unhandled interrupt: cause=%d\n", cause); break;
    }
  } else {
    switch (cause) {
    case MCAUSE_ECALL_M: syscall_handler(tf); break;
    case MCAUSE_ECALL_U: syscall_handler(tf); break;
    case MCAUSE_INST_PF:
    case MCAUSE_LOAD_PF:
    case MCAUSE_STORE_PF:
      tf->mepc += 4;
      break;
    default: tf->mepc += 4; break;
    }
  }
}
