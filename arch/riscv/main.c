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
  /* Warmup ecall (SYS_YIELD instead of SYS_GETPID for RV32 compat) */
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
  fs_init();
  fdtable_init();

  /* Write user program binary to ramfs using direct FS calls */
  {
    static const unsigned char hello_bin[] = {
        0x85, 0x48, 0x05, 0x45, 0x97, 0x05, 0x00, 0x00, 0xd1, 0x05,
        0x51, 0x46, 0x73, 0x00, 0x00, 0x00, 0x8d, 0x48, 0x73, 0x00,
        0x00, 0x00, 0xed, 0xbf, 0x48, 0x65, 0x6c, 0x6c, 0x6f, 0x20,
        0x66, 0x72, 0x6f, 0x6d, 0x20, 0x72, 0x61, 0x6d, 0x66, 0x73,
        0x21, 0x0a, 0x00, 0x00};
    Inode *bin = ialloc("/bin/hello");
    if (bin) {
      igrow(bin, sizeof(hello_bin));
      memcpy(bin->data, hello_bin, sizeof(hello_bin));
      bin->size = sizeof(hello_bin);
      printk("[boot] wrote /bin/hello (%d bytes)\n", sizeof(hello_bin));
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

  uart_init();
  plic_init();
  virtio_blk_init();

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

  /* usr_init(); usr_spawn(); */

  vmm_init();
  proc_create(proc_a, "A");
  proc_create(proc_b, "B");
  scheduler();
}
