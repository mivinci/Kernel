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
#include <fs.h>
#include <syscall.h>

static unsigned long count_a, count_b;

static void proc_a(void) {
  printk("[A] started\n");
  int pid = syscall(SYS_GETPID, 0, 0, 0);
  printk("[A] pid=%d\n", pid);

  for (;;) {
    count_a++;
    if (count_a % 1000000 == 0)
      printk("[A] tick\n");
    yield();
  }
}

static void proc_b(void) {
  printk("[B] started\n");
  for (;;) {
    count_b++;
    if (count_b % 1000000 == 0)
      printk("[B] tick\n");
    syscall(SYS_YIELD, 0, 0, 0);
  }
}

void main(int hartid) {
  printk("[kernel] Booting by hart %d ...\n", hartid);

  trap_init();
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
  vmm_init();

  /* FS test: direct function calls (no ecall) */
  printk("[fs] test: direct ialloc/iname\n");
  Inode *ip = ialloc("/hello");
  printk("[fs] ip=%p\n", ip);
  if (ip) {
    igrow(ip, 32);
    memcpy(ip->data, "Hello, filesystem!", 19);
    ip->size = 19;
  }
  ip = iname("/hello");
  if (ip) {
    printk("[fs] read: ");
    for (int i = 0; i < ip->size; i++)
      putc(ip->data[i]);
    putc('\n');
  }

  /* FS test via ecall */
  printk("[fs] test: creating /hello\n");
  int fd = syscall(SYS_OPEN, (unsigned long)"/hello2", 1, 0);
  printk("[fs] fd=%d\n", fd);
  if (fd >= 0) {
    const char *msg = "Hello, filesystem!";
    syscall(SYS_WRITE, fd, (unsigned long)msg, strlen(msg));
    syscall(SYS_CLOSE, fd, 0, 0);
  }

  fd = syscall(SYS_OPEN, (unsigned long)"/hello", 0, 0);
  printk("[fs] read fd=%d\n", fd);
  if (fd >= 0) {
    char buf[64];
    int n = syscall(SYS_READ, fd, (unsigned long)buf, sizeof(buf) - 1);
    if (n > 0) {
      buf[n] = '\0';
      printk("[fs] read: %s\n", buf);
    }
    syscall(SYS_CLOSE, fd, 0, 0);
  }

  proc_create(proc_a, "A");
  proc_create(proc_b, "B");

  scheduler();
}
