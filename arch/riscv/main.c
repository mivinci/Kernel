#include <arch/riscv/mmu.h>
#include <arch/riscv/plic.h>
#include <arch/riscv/timer.h>
#include <arch/riscv/trap.h>
#include <arch/riscv/virtio.h>
#include <fdt.h>
#include <fs.h>
#include <kernel.h>
#include <types.h>
#include <pmm.h>
#include <proc.h>
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
  printk("[kernel] Booting by hart %d ...\n", hartid);

  trap_init();
  fdt_init(fdt);
  fdt_apply();

  pmm_init();
  timer_init();
  proc_init();

  uart_init();
  plic_init();
  virtio_blk_init();

  fs_init();
  fdtable_init();

  /*
   * Register user binaries from the block device.
   * Only metadata is stored; data is read on demand when
   * usr_spawn loads a program (usr_load → virtio_blk_read).
   */
  if (virtio_blk_capacity() > 0) {
    char s[SECTOR_SIZE];
    if (virtio_blk_read(0, s) == 0) {
      unsigned int magic = *(unsigned int *)(s + 0);
      unsigned int nfile = *(unsigned int *)(s + 4);
      if (magic == 0x52414D46 && nfile > 0 && nfile <= 6) {
        for (unsigned int i = 0; i < nfile; i++) {
          char        *entry   = s + 8 + i * 72;
          char        *name    = entry;
          unsigned int fsize   = *(unsigned int *)(entry + 64);
          unsigned int fsector = *(unsigned int *)(entry + 68);
          Inode *ip = idiskslot(name, fsector, fsize);
          if (ip)
            printk("[boot] %-16s sector=%d size=%d\n", name, fsector, fsize);
          else
            printk("[boot] %-16s slot full\n", name);
        }
      }
    }
  }

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
  scheduler();
}
