#include <kernel.h>
#include <arch/riscv/trap.h>
#include <arch/riscv/timer.h>

void main(int hartid) {
  printk("[kernel] Booting by hart %d ...\n", hartid);

  trap_init();
  timer_init();

  for (;;) {
    /* Wait for interrupt — timer interrupts will wake this hart. */
    __asm__ __volatile__("wfi");
  }
}
