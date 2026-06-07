#include <arch/riscv/csr.h>
#include <arch/riscv/trap.h>
#include <fs.h>
#include <kernel.h>
#include <types.h>
#include <pmm.h>
#include <proc.h>
#include <usr.h>

/*
 * User code runs in U-mode with bare physical addressing.
 * Each process gets its own physical page (upage) for the user
 * binary, allocated by usr_load() and stored in Proc.upage.
 */

void usr_init(void) {
  printk("[usr] ready (per-process pages, bare addressing)\n");
}

int usr_load(const char *path, void **out_page, int *out_size) {
  Inode *ip = iname(path);
  if (!ip) {
    printk("[usr] not found: %s\n", path);
    return -1;
  }

  if (ip->size <= 0 || ip->size > PAGE_SIZE) {
    printk("[usr] bad size for %s: %d\n", path, ip->size);
    return -1;
  }

  void *page = kalloc();
  if (!page) {
    printk("[usr] kalloc failed for %s\n", path);
    return -1;
  }

  memcpy(page, ip->data, ip->size);
  *out_page = page;
  *out_size = ip->size;

  printk("[usr] loaded %s: %d bytes at %p\n", path, ip->size, page);
  return 0;
}

/*
 * Enter U-mode. Sets MPP=0 and does mret to the user program's
 * physical address (read from Proc.upage).
 * Retry loop handles spurious mret faults (TLB warmup, PMP).
 */
__attribute__((noreturn)) void usr_enter(void) {
  Proc *p = cpu_proc();
  if (!p || !p->upage) {
    printk("[usr] enter: no user page for pid=%d\n", p ? p->pid : -1);
    proc_exit(-1);
  }

  unsigned long ms = csr_read(mstatus);
  ms &= ~MSTATUS_MPP;      /* MPP = 0 (U-mode) */
  ms |= MSTATUS_MPIE;
  csr_write(mstatus, ms);

  /* Configure PMP: allow U-mode access to all physical memory */
  csr_write(pmpaddr0, -1UL);
  csr_write(pmpcfg0, 0x1FUL);

  int retry = 0;
  for (;;) {
    csr_write(mepc, (unsigned long)p->upage);
    __asm__ __volatile__("mret");
    /* mret faults: trap_handler advances tf->mepc past mret,
     * so we land here and retry. After a few iterations the
     * TLB is warm and mret succeeds, never returning here. */
    retry++;
    if (retry > 100) {
      printk("[usr] mret stuck after 100 retries, exiting\n");
      proc_exit(-1);
    }
  }
}

int usr_spawn(const char *path) {
  void *page;
  int   size;

  if (usr_load(path, &page, &size) < 0)
    return -1;

  int pid = proc_create((void (*)(void))usr_enter, path, page);
  if (pid < 0) {
    kfree(page);
    return -1;
  }

  printk("[usr] spawned %s as pid=%d\n", path, pid);
  return pid;
}
