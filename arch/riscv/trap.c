#include <arch/riscv/arch.h>
#include <arch/riscv/csr.h>
#include <arch/riscv/plic.h>
#include <arch/riscv/timer.h>
#include <arch/riscv/trap.h>
#include <kernel.h>
#include <syscall.h>

extern void trap_entry(void);

void trap_init(void) {
  /*
   * Set the machine trap vector to our trap_entry.
   * Mode 0 = direct (all traps go to the same address).
   */
  csr_write(mtvec, (unsigned long)trap_entry);

  /*
   * Enable machine-mode interrupts globally in mstatus.
   * Individual interrupt sources must still be enabled via mie.
   */
  csr_write(mstatus, csr_read(mstatus) | MSTATUS_MIE);

  printk("[trap] mtvec=%p mstatus=%p\n", trap_entry, csr_read(mstatus));
}

void trap_handler(TrapFrame *tf) {
  /*
   * mcause high bit indicates interrupt vs exception.
   * RV32: bit 31, RV64: bit 63.
   */
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
    default:
      printk("[trap] exception: mepc=%p mcause=%d mtval=%p\n", tf->mepc, cause, tf->mtval);
      tf->mepc += 4;
      break;
    }
  }
}
