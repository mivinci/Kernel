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
#include <usr.h>

static unsigned long count_a, count_b;

static void proc_a(void) {
  printk("[A] started\n");
  syscall(SYS_YIELD, 0, 0, 0);
  printk("[A] after warmup\n");

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

  /* Test block I/O */
  if (virtio_blk_capacity() > 0) {
    char r[SECTOR_SIZE], w[SECTOR_SIZE];
    printk("[blk] read sec0:  %s\n", virtio_blk_read(0, r) == 0 ? "OK" : "FAIL");
    for (int i = 0; i < SECTOR_SIZE; i++) w[i] = (char)(i & 0xff);
    printk("[blk] write sec0: %s\n", virtio_blk_write(0, w) == 0 ? "OK" : "FAIL");
    char v[SECTOR_SIZE];
    int ok = virtio_blk_read(0, v) == 0;
    for (int i = 0; ok && i < SECTOR_SIZE; i++) { if (v[i] != w[i]) { ok = 0; break; } }
    printk("[blk] verify:    %s\n", ok ? "OK" : "FAIL");
  }

  usr_init();
  usr_spawn("/bin/init");

  vmm_init();
  proc_create(proc_a, "A", NULL);
  proc_create(proc_b, "B", NULL);
  scheduler();
}
