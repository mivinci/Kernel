#include <kernel.h>
#include <arch/riscv/trap.h>
#include <arch/riscv/timer.h>
#include <pmm.h>

void main(int hartid) {
  printk("[kernel] Booting by hart %d ...\n", hartid);

  trap_init();
  pmm_init();
  timer_init();

  /* Quick smoke test: allocate and free a page */
  void *page = kalloc();
  printk("[pmm] test: kalloc -> %p\n", page);
  kfree(page);
  void *page2 = kalloc();
  printk("[pmm] test: kalloc again -> %p (should match)\n", page2);
  kfree(page2);

  for (;;) {
    __asm__ __volatile__("wfi");
  }
}
