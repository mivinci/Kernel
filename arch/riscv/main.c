#include <kernel.h>
#include <arch/riscv/trap.h>

void main(int hartid) {
  printk("[kernel] Booting by hart %d ...\n", hartid);

  trap_init();

  for (;;) {
    /* Wait for interrupt — the kernel is now event-driven.
     * Currently no interrupts are enabled, so this just spins.
     * Timer interrupts (Phase 2) will break this idle loop. */
    __asm__ __volatile__("wfi");
  }
}
