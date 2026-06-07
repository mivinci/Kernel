#include <arch/riscv/arch.h>
#include <arch/riscv/csr.h>
#include <arch/riscv/mmu.h>
#include <kernel.h>
#include <pmm.h>

/* Map this range with 2 MB megapages */
#define MAP_START 0x80000000UL
#define MAP_SIZE  (128UL * 1024 * 1024) /* 128 MB */
#define MAP_END   (MAP_START + MAP_SIZE)

#define MEGAPAGE_SIZE (2UL * 1024 * 1024) /* 2 MB */

void vmm_init(void) {
#if __riscv_xlen == 32
  printk("[vmm] skipped — Sv32 not yet implemented\n");
  return;
#endif
  PageTableEntry *pgd = (PageTableEntry *)kalloc();
  PageTableEntry *pmd = (PageTableEntry *)kalloc();

  unsigned long flags = PTE_V | PTE_R | PTE_W | PTE_X | PTE_U;

  for (unsigned long pa = MAP_START; pa < MAP_END; pa += MEGAPAGE_SIZE) {
    unsigned long vpn = (pa >> 21) & 0x1FF; /* VPN[1] for 2MB megapage */
    pmd[vpn].raw      = PA2PTE(pa) | flags;
  }

  /* PGD entry: VPN[2] = 0x80000000 >> 30 = 2 */
  unsigned long pgd_idx = (MAP_START >> 30) & 0x1FF;
  pgd[pgd_idx].raw      = PA2PTE((unsigned long)pmd) | PTE_V;

  /* Enable Sv39 paging */
  unsigned long satp = MAKE_SATP((unsigned long)pgd);

  printk("[vmm] pgd=%p pmd=%p satp=%p\n", pgd, pmd, satp);

  /* sfence.vma before and after satp write */
  __asm__ __volatile__("sfence.vma x0, x0");
  csr_write(satp, satp);
  __asm__ __volatile__("sfence.vma x0, x0");

  printk("[vmm] Sv39 paging enabled, identity map 128 MB\n");

  volatile unsigned long *test = (volatile unsigned long *)0x87F00000UL;
  *test                         = 0xDEADBEEF;
  if (*test == 0xDEADBEEF)
    printk("[vmm] test: r/w OK at 0x%p\n", test);
  else
    printk("[vmm] test: FAIL\n");
}
