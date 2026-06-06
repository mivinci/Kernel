#include <kernel.h>
#include <libc.h>
#include <arch/riscv/trap.h>
#include <arch/riscv/timer.h>
#include <arch/riscv/spinlock.h>
#include <arch/riscv/plic.h>
#include <arch/riscv/mmu.h>
#include <uart.h>
#include <pmm.h>
#include <proc.h>
#include <syscall.h>

static unsigned long count_a, count_b;

static void proc_a(void) {
  printk("[A] started\n");
  int pid = syscall(SYS_GETPID, 0, 0, 0);
  for (;;) {
    count_a++;
    if (count_a % 1000000 == 0) {
      const char *msg = "[A] tick\n";
      syscall(SYS_WRITE, 1, (unsigned long)msg, strlen(msg));
    }
    syscall(SYS_YIELD, 0, 0, 0);
  }
}

static void proc_b(void) {
  int pid = syscall(SYS_GETPID, 0, 0, 0);
  printk("[B] pid=%d started\n", pid);
  for (;;) {
    count_b++;
    if (count_b % 1000000 == 0) {
      const char *msg = "[B] tick\n";
      syscall(SYS_WRITE, 1, (unsigned long)msg, strlen(msg));
    }
    syscall(SYS_YIELD, 0, 0, 0);
  }
}

void main(int hartid) {
  printk("[kernel] Booting by hart %d ...\n", hartid);

  trap_init();
  pmm_init();
  timer_init();
  proc_init();

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

  /* Create test processes */
  proc_create(proc_a, "A");
  proc_create(proc_b, "B");

  /* Enter scheduler (never returns) */
  scheduler();
}
