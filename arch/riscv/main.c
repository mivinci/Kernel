#include <kernel.h>
#include <arch/riscv/trap.h>
#include <arch/riscv/timer.h>
#include <arch/riscv/spinlock.h>
#include <arch/riscv/plic.h>
#include <arch/riscv/mmu.h>
#include <uart.h>
#include <pmm.h>

void main(int hartid) {
  printk("[kernel] Booting by hart %d ...\n", hartid);

  trap_init();
  pmm_init();
  timer_init();

  /* PMM smoke test */
  void *page = kalloc();
  printk("[pmm] test: kalloc -> %p\n", page);
  kfree(page);
  void *page2 = kalloc();
  printk("[pmm] test: kfree/kalloc -> %p (same)\n", page2);
  kfree(page2);

  /* Spinlock smoke test */
  SpinLock lk;
  spin_init(&lk);
  spin_lock(&lk);
  spin_unlock(&lk);
  printk("[spinlock] lock/unlock passed\n");

  /* PLIC and UART interrupt-driven receive (before MMU enable) */
  uart_init();
  plic_init();

  /* Sv39 identity mapping and MMU enable */
  vmm_init();

  for (;;) {
    __asm__ __volatile__("wfi");
  }
}
