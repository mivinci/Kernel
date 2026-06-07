#include <arch/riscv/csr.h>
#include <arch/riscv/trap.h>
#include <diskfs.h>
#include <kernel.h>
#include <types.h>
#include <pmm.h>
#include <proc.h>
#include <usr.h>

void usr_init(void) {
  printk("[usr] ready\n");
}

int usr_load(const char *path, void **out_page, int *out_size) {
  int fsize = 0;
  int inum  = dfs_open(path, &fsize);
  if (inum < 0) {
    printk("[usr] not found: %s\n", path);
    return -1;
  }
  if (fsize <= 0 || fsize > PAGE_SIZE) {
    printk("[usr] bad size for %s: %d\n", path, fsize);
    return -1;
  }

  void *page = kalloc();
  if (!page) { printk("[usr] kalloc failed\n"); return -1; }

  if (dfs_read(inum, page, 0, fsize) != fsize) {
    printk("[usr] disk read failed\n"); kfree(page); return -1;
  }

  *out_page = page;
  *out_size = fsize;
  printk("[usr] loaded %s via diskfs (%d bytes)\n", path, fsize);
  return 0;
}

__attribute__((noreturn)) void usr_enter(void) {
  Proc *p = cpu_proc();
  if (!p || !p->upage) { proc_exit(-1); }

  unsigned long ms = csr_read(mstatus);
  ms &= ~MSTATUS_MPP;      /* MPP = 0 (U-mode) */
  ms |= MSTATUS_MPIE;
  csr_write(mstatus, ms);

  /*
   * Configure PMP (physical memory protection):
   *   Entry 0 (TOR, no U-mode access): [0, _end) — kernel code/data
   *   Entry 1 (TOR, U-mode RWX):       [_end, MAX) — user memory
   *
   * pmpaddr stores addr >> 2 (4-byte granularity).
   * pmpcfg bits per entry (1 byte each):
   *   0x08 = TOR, no U-mode R/W/X
   *   0x0F = TOR, U-mode R+W+X
   */
  extern char _end[];
  unsigned long end = (unsigned long)_end;
  end = (end + PAGE_SIZE - 1) & ~(PAGE_SIZE - 1UL);

  csr_write(pmpaddr0, end >> 2);
  csr_write(pmpaddr1, -1UL);
  csr_write(pmpcfg0, 0x0F08UL);  /* entry1=0x0F, entry0=0x08 */

  int retry = 0;
  for (;;) {
    csr_write(mepc, (unsigned long)p->upage);
    __asm__ __volatile__("mret");
    if (++retry > 100) proc_exit(-1);
  }
}

int usr_spawn(const char *path) {
  void *page;
  int   size;
  if (usr_load(path, &page, &size) < 0) return -1;
  int pid = proc_create((void (*)(void))usr_enter, path, page);
  if (pid < 0) { kfree(page); return -1; }
  printk("[usr] spawned %s as pid=%d\n", path, pid);
  return pid;
}
