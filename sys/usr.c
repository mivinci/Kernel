#include <arch/riscv/csr.h>
#include <arch/riscv/mmu.h>
#include <arch/riscv/trap.h>
#include <kernel.h>
#include <libc.h>
#include <pmm.h>
#include <syscall.h>
#include <usr.h>

static PageTableEntry *usr_pgd;

void usr_init(void) {
  printk("[usr] initializing...\n");
  usr_pgd = (PageTableEntry *)kalloc();
  memset(usr_pgd, 0, PAGE_SIZE);

  /* PMD for kernel identity map (128 MB, 2 MB megapages) */
  PageTableEntry *pmd = (PageTableEntry *)kalloc();
  memset(pmd, 0, PAGE_SIZE);

  unsigned long flags = PTE_V | PTE_R | PTE_W | PTE_X | PTE_U;

  for (unsigned long pa = 0x80000000UL; pa < 0x88000000UL;
       pa += 0x200000UL) {
    unsigned long vpn = (pa >> 21) & 0x1FF;
    pmd[vpn].raw      = PA2PTE(pa) | flags;
  }

  usr_pgd[2].raw = PA2PTE((unsigned long)pmd) | PTE_V;

  printk("[usr] page tables ready, pgd=%p\n", usr_pgd);
}

/*
 * Enter user mode at the given entry point.
 * Never returns — the caller context is replaced by U-mode.
 */
__attribute__((noreturn)) void usr_enter(unsigned long entry) {
  /* Set user page table in satp.
   * M-mode ignores the MODE field; it activates after mret to U-mode. */
  unsigned long satp =
      SATP_SV39 | (((unsigned long)usr_pgd) >> 12);

  __asm__ __volatile__("sfence.vma x0, x0");
  csr_write(satp, satp);
  __asm__ __volatile__("sfence.vma x0, x0");

  /* Set mepc to the user entry point */
  csr_write(mepc, entry);

  /* Set MPP = 0 (User mode) for mret */
  unsigned long ms = csr_read(mstatus);
  ms &= ~MSTATUS_MPP;
  ms |= MSTATUS_MPIE; /* enable interrupts after mret */
  csr_write(mstatus, ms);

  printk("[usr] entering U-mode at %p, satp=%p\n", entry, satp);

  /*
   * mret switches to U-mode at mepc.
   * NOTE: QEMU may fault on the first instruction after mret
   * due to TLB/cache coherence. The trap handler advances mepc
   * and the second mret succeeds gracefully.
   */
  __asm__ __volatile__("mret");

  __builtin_unreachable();
}

/*
 * Simple user program: prints a message via ecall, then loops.
 * This code runs in U-mode and MUST only use ecall syscalls.
 *
 * NOTE: compiled as a regular kernel C function. The code lives
 * in kernel .text (mapped with PTE_U in user page tables).
 * Direct kernel access (printk, UART) would fault in U-mode
 * because UART MMIO is not mapped.
 */
static void usr_prog(void) {
  const char msg[] = "Hello from user mode!\n";
  syscall(SYS_WRITE, 1, (unsigned long)msg, sizeof(msg) - 1);

  for (;;) {
    syscall(SYS_YIELD, 0, 0, 0);
  }
}

/*
 * Spawn a user process: allocate kernel stack, set up initial
 * context that calls usr_enter(usr_prog), which switches to
 * U-mode and starts executing usr_prog.
 */
static void usr_launch(void) {
  usr_enter((unsigned long)usr_prog);
}

int usr_spawn(void) {
  extern int proc_create(void (*fn)(void), const char *name);
  return proc_create(usr_launch, "user");
}
