#include <arch/riscv/arch.h>
#include <arch/riscv/csr.h>
#include <arch/riscv/mmu.h>
#include <kernel.h>
#include <pmm.h>
#include <proc.h>

/*
 * Boot-time identity map: 128 MB at 0x80000000 with 2MB megapages.
 * No PTE_U — M-mode doesn't use paging.
 */
#define MAP_START 0x80000000UL
#define MAP_SIZE  (128UL * 1024 * 1024)
#define MAP_END   (MAP_START + MAP_SIZE)
#define MEGAPAGE  (2UL * 1024 * 1024)

static PageTableEntry *kernel_pgd;

void vmm_init(void) {
#if __riscv_xlen == 32
  printk("[vmm] skipped — Sv32\n");
  return;
#endif
  PageTableEntry *pgd = (PageTableEntry *)kalloc();
  PageTableEntry *pmd = (PageTableEntry *)kalloc();
  kernel_pgd = pgd;

  unsigned long flags = PTE_V | PTE_R | PTE_W | PTE_X; /* no PTE_U */

  for (unsigned long pa = MAP_START; pa < MAP_END; pa += MEGAPAGE) {
    unsigned long vpn = (pa >> 21) & 0x1FF;
    pmd[vpn].raw = PA2PTE(pa) | flags;
  }

  unsigned long pgd_idx = (MAP_START >> 30) & 0x1FF;
  pgd[pgd_idx].raw = PA2PTE((unsigned long)pmd) | PTE_V;

  unsigned long satp = MAKE_SATP((unsigned long)pgd);
  __asm__ __volatile__("sfence.vma x0, x0");
  csr_write(satp, satp);
  __asm__ __volatile__("sfence.vma x0, x0");

  printk("[vmm] pgd=%p satp=%p  (128 MB, M-mode only)\n", pgd, satp);
}

/*
 * Create a user page table: identity-maps 128 MB (so the kernel
 * can access user addresses directly via VA=PA), plus maps the
 * process's upage at virtual address 0 with PTE_U for user-mode
 * execution.
 *
 * Structure:  PGD[0]  → PMD_user  → PTB    → upage (VA 0,  U-accessible)
 *             PGD[2]  → PMD_kernel → 2 MB megapages (VA=PA, M-only)
 *
 * Returns satp value; caller frees via vmm_free_user_pgdir().
 */
unsigned long vmm_create_user_pgdir(void *upage) {
  PageTableEntry *pgd = (PageTableEntry *)kalloc();
  if (!pgd) return 0;

  /* Identity-map kernel region: PGD[2] → PMD with 2MB megapages.
   * This allows the kernel (M-mode, bare addressing bypasses paging)
   * and user-mode page-faults if U code tries kernel addresses. */
  PageTableEntry *pmd_kern = (PageTableEntry *)kalloc();
  if (!pmd_kern) { kfree(pgd); return 0; }
  unsigned long fl = PTE_V | PTE_R | PTE_W | PTE_X; /* no PTE_U */
  for (unsigned long pa = MAP_START; pa < MAP_END; pa += MEGAPAGE) {
    unsigned long vpn = (pa >> 21) & 0x1FF;
    pmd_kern[vpn].raw = PA2PTE(pa) | fl;
  }
  unsigned long kidx = (MAP_START >> 30) & 0x1FF;
  pgd[kidx].raw = PA2PTE((unsigned long)pmd_kern) | PTE_V;

  /* User mapping at VA 0: PGD[0] → PMD → PTB → upage (PTE_U) */
  PageTableEntry *pmd = (PageTableEntry *)kalloc();
  PageTableEntry *ptb = (PageTableEntry *)kalloc();
  if (!pmd || !ptb) {
    if (pgd) kfree(pgd);
    if (pmd_kern) kfree(pmd_kern);
    if (pmd) kfree(pmd);
    if (ptb) kfree(ptb);
    return 0;
  }
  pmd[0].raw = PA2PTE((unsigned long)ptb) | PTE_V;
  ptb[0].raw = PA2PTE((unsigned long)upage) | PTE_V | PTE_R | PTE_W | PTE_X | PTE_U;
  pgd[0].raw = PA2PTE((unsigned long)pmd) | PTE_V;

  return MAKE_SATP((unsigned long)pgd);
}

void vmm_free_user_pgdir(unsigned long satp) {
  if (!satp) return;
  unsigned long pgd_pa = (satp & SATP_PPN) << 12;
  PageTableEntry *pgd = (PageTableEntry *)pgd_pa;

  unsigned long kidx = (MAP_START >> 30) & 0x1FF;
  PageTableEntry *pmd_kern = (PageTableEntry *)PTE2PA(pgd[kidx].raw);
  if (pmd_kern) kfree(pmd_kern);

  PageTableEntry *pmd = (PageTableEntry *)PTE2PA(pgd[0].raw);
  PageTableEntry *ptb = (PageTableEntry *)PTE2PA(pmd[0].raw);
  if (ptb) kfree(ptb);
  if (pmd) kfree(pmd);
  kfree(pgd);
}

/*
 * Walk the current process's page table to translate a user
 * virtual address to a physical address.  Returns 0 if not mapped.
 */
unsigned long user_va2pa(unsigned long va) {
  Proc *p = cpu_proc();
  if (!p || !p->satp) return 0;

  unsigned long pgd_pa = (p->satp & SATP_PPN) << 12;
  PageTableEntry *pgd = (PageTableEntry *)pgd_pa;

  unsigned long vpn2 = (va >> 30) & 0x1FF;
  unsigned long vpn1 = (va >> 21) & 0x1FF;
  unsigned long vpn0 = (va >> 12) & 0x1FF;

  if (!(pgd[vpn2].raw & PTE_V)) return 0;
  PageTableEntry *pmd = (PageTableEntry *)PTE2PA(pgd[vpn2].raw);
  if (!(pmd[vpn1].raw & PTE_V)) return 0;

  if (pmd[vpn1].raw & (PTE_R | PTE_W | PTE_X)) {
    /* 2 MB megapage */
    return PTE2PA(pmd[vpn1].raw) | (va & 0x1FFFFF);
  }

  /* 4 KB page table */
  PageTableEntry *ptb = (PageTableEntry *)PTE2PA(pmd[vpn1].raw);
  if (!(ptb[vpn0].raw & PTE_V)) return 0;
  return PTE2PA(ptb[vpn0].raw) | (va & 0xFFF);
}
