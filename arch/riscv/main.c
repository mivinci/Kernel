#include <arch/riscv/mmu.h>
#include <arch/riscv/plic.h>
#include <arch/riscv/timer.h>
#include <arch/riscv/trap.h>
#include <arch/riscv/virtio.h>
#include <diskfs.h>
#include <fdt.h>
#include <fs.h>
#include <kernel.h>
#include <types.h>
#include <pmm.h>
#include <proc.h>
#include <tty.h>
#include <uart.h>
#include <usr.h>

/*
 * Init path — the first user-mode process.
 * Like Unix System V /etc/inittab, the kernel tries each
 * path in order and runs the first one that exists in ramfs.
 * Change INIT_PATH if your init has a different name/location.
 */
#ifndef INIT_PATH
#define INIT_PATH "/bin/init"
#endif

static const char *init_fallbacks[] = {
  INIT_PATH,      /* try user-specified first    */
  "/bin/sh",      /* then a shell                */
  "/bin/hello",   /* finally the smoke-test app  */
};


void kmain(int hartid, void *fdt) {
  /*
   * Guard against re-entry.  If kmain() is called a second time
   * (e.g. after a stack overflow or exception that accidentally
   * jumps back to _start), skip the full init sequence so we
   * don't double-allocate pages, corrupt the FDT, or reset the
   * process table.  Go straight to the scheduler so surviving
   * harts and processes keep running.
   */
  static int booted = 0;
  if (booted) {
    scheduler(hartid);
  }
  booted = 1;

  printk("[kernel] Booting by hart %d ...\n", hartid);

  trap_init();
  fdt_init(fdt);
  fdt_apply();

  pmm_init();
  timer_init();
  proc_init();

  uart_init();
  tty_init(&console_tty);
  plic_init();
  virtio_blk_init();

  fs_init();
  fdtable_init();

  dfs_init();

  /* PMM smoke test */
  void *page = kalloc();
  printk("[pmm] test: kalloc -> %p\n", page);
  kfree(page);
  void *page2 = kalloc();
  printk("[pmm] test: kfree/kalloc -> %p (same)\n", page2);
  kfree(page2);

  usr_init();

  /* Try init paths in order — first one on disk wins */
  int pid = -1;
  for (int i = 0; i < 3; i++) {
    pid = usr_spawn(init_fallbacks[i]);
    if (pid >= 0) {
      printk("[boot] init: %s (pid=%d)\n", init_fallbacks[i], pid);
      break;
    }
  }
  if (pid < 0)
    printk("[boot] PANIC: no init found, idle loop\n");

  vmm_init();

  /* Signal other harts that the kernel is ready */
  extern int hart_ready;
  hart_ready = 1;
  __asm__ __volatile__("fence w,w" ::: "memory");

  scheduler(hartid);
}
