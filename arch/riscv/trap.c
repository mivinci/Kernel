#include <arch/riscv/arch.h>
#include <arch/riscv/csr.h>
#include <arch/riscv/plic.h>
#include <arch/riscv/timer.h>
#include <arch/riscv/trap.h>
#include <kernel.h>
#include <syscall.h>

extern void trap_entry(void);

/*
 * WARNING: Do NOT call printk() from trap_handler or any function
 * it calls (syscall/exception/interrupt paths).  printk uses a
 * 1024-byte vsprintf buffer on the stack.  Our kernel stack is
 * only 4 KB (KSTACK = PAGE_SIZE).  With the 288-byte trap frame
 * already on the stack, printk's 1024-byte buffer leaves less
 * than 2.7 KB of headroom.  If a fault triggers another printk
 * before the stack unwinds, the second buffer overflows into
 * the trap frame, zeroing mepc and causing mret to jump to
 * address 0 — an infinite fault loop.
 *
 * For debugging, read QEMU's mcause/mepc from the trap CSRs
 * via `qemu-system-riscv64 -d int,guest_errors`.
 */

/*
 * Initialize trap handling for this hart.
 *
 * ORDER MATTERS: mscratch must be written BEFORE enabling
 * MSTATUS_MIE.  If a timer or PLIC interrupt is pending
 * (common on non-0 harts that initialized late), the
 * trap_entry csrrw will swap sp with mscratch.  If mscratch
 * is garbage (0), sp becomes 0 → all frame STOREs fault.
 */
void trap_init(void) {
  unsigned long sp;
  __asm__ __volatile__("mv %0, sp" : "=r"(sp));
  csr_write(mscratch, sp);

  csr_write(mtvec, (unsigned long)trap_entry);
  csr_write(mstatus, csr_read(mstatus) | MSTATUS_MIE);

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
    default: tf->mepc += 4; break; /* no printk — see warning above */
    }
  }
}
