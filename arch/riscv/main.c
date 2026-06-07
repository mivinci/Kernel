#include <arch/riscv/mmu.h>
#include <arch/riscv/plic.h>
#include <arch/riscv/spinlock.h>
#include <arch/riscv/timer.h>
#include <arch/riscv/trap.h>
#include <arch/riscv/virtio.h>
#include <fdt.h>
#include <fs.h>
#include <kernel.h>
#include <libc.h>
#include <pmm.h>
#include <proc.h>
#include <syscall.h>
#include <uart.h>

static unsigned long count_a, count_b;

static void proc_a(void) {
  printk("[A] started\n");
  int pid = syscall(SYS_GETPID, 0, 0, 0);
  printk("[A] pid=%d\n", pid);

  for (;;) {
    count_a++;
    if (count_a % 1000000 == 0) printk("[A] tick\n");
    yield();
  }
}

static void proc_b(void) {
  printk("[B] started\n");
  for (;;) {
    count_b++;
    if (count_b % 1000000 == 0) printk("[B] tick\n");
    syscall(SYS_YIELD, 0, 0, 0);
  }
}

void kmain(int hartid, void *fdt) {
  printk("[kernel] Booting by hart %d ...\n", hartid);

  trap_init();

  /* Parse device tree */
  printk("[fdt] pointer=%p\n", fdt);
  fdt_init(fdt);

  /* Print detected hardware */
  unsigned long mem_base, mem_size;
  unsigned long uart_addr, plic_addr;
  fdt_get_memory(&mem_base, &mem_size);
  fdt_get_uart(&uart_addr);
  fdt_get_plic(&plic_addr);
  printk("[fdt] RAM: %p-%p (%d MB)\n", mem_base, mem_base + mem_size, mem_size / (1024 * 1024));
  printk("[fdt] UART=%p PLIC=%p\n", uart_addr, plic_addr);
  pmm_init();
  timer_init();
  proc_init();
  fs_init();
  fdtable_init();

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

  uart_init();
  plic_init();
  virtio_blk_init();

  vmm_init();
  proc_create(proc_a, "A");
  proc_create(proc_b, "B");
  scheduler();
}
