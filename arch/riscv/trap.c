#include <kernel.h>
#include <arch/riscv/trap.h>
#include <arch/riscv/csr.h>
#include <arch/riscv/timer.h>

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

void trap_handler(struct TrapFrame *tf) {
  /*
   * mcause high bit indicates interrupt vs exception.
   * Interrupt: mcause[63] == 1 (unsigned, top bit set)
   * Exception: mcause[63] == 0
   */
  unsigned long cause = tf->mcause;
  unsigned long is_intr = cause >> 63;

  if (is_intr) {
    cause &= ~(1UL << 63);
    switch (cause) {
    case MCAUSE_MTIMER:
      timer_handle();
      break;
    case MCAUSE_MEXT:
      /* TODO: external interrupt handler (Phase 6) */
      break;
    case MCAUSE_MSI:
      /* TODO: software interrupt handler (Phase 4) */
      break;
    default:
      printk("[trap] unhandled interrupt: cause=%d\n", cause);
      break;
    }
  } else {
    printk("[trap] exception: mepc=%p mcause=%d mtval=%p\n",
           tf->mepc, cause, tf->mtval);
    /* Increment mepc past the faulting instruction for resumable exceptions.
     * For fatal exceptions the kernel will halt. */
    tf->mepc += 4;
  }
}
