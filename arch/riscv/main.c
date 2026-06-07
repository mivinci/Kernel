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
   * Load user programs from block device into ramfs.
   * Sector 0 header: [magic:4][count:4][entries:count*72]
   *   entry = name[64] | size[4] | sector[4]
   * File data at the named sector.
   * Built by tools/mkdisk.py.
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
          char         buf[SECTOR_SIZE];
          if (fsize > SECTOR_SIZE) continue;
          if (virtio_blk_read(fsector, buf) != 0) continue;
          Inode *bin = ialloc(name);
          if (bin) {
            igrow(bin, fsize);
            memcpy(bin->data, buf, fsize);
            bin->size = fsize;
            printk("[boot] loaded %s from disk (%d bytes)\n", name, fsize);
          }
        }
      }
    }
  }

  /* Smoke tests */
  void *page = kalloc();
  printk("[pmm] test: kalloc -> %p\n", page);
  kfree(page);
  void *page2 = kalloc();
  printk("[pmm] test: kfree/kalloc -> %p (same)\n", page2);
  kfree(page2);

  /* Test block I/O on a safe sector (not 0 — that's the header) */
  if (virtio_blk_capacity() > 1) {
    char w[SECTOR_SIZE], v[SECTOR_SIZE];
    for (int i = 0; i < SECTOR_SIZE; i++) w[i] = (char)(i & 0xff);
    printk("[blk] write sec1: %s\n", virtio_blk_write(1, w) == 0 ? "OK" : "FAIL");
    int ok = virtio_blk_read(1, v) == 0;
    for (int i = 0; ok && i < SECTOR_SIZE; i++) { if (v[i] != w[i]) { ok = 0; break; } }
    printk("[blk] verify:   %s\n", ok ? "OK" : "FAIL");
  }

  usr_init();
  usr_spawn("/bin/init");

  vmm_init();
  scheduler();
}
