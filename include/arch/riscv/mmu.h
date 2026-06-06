#ifndef _ARCH_RISCV_MMU_H
#define _ARCH_RISCV_MMU_H

#include <libc.h>

/* Sv39 page table entry flags */
#define PTE_V (1UL << 0) /* Valid */
#define PTE_R (1UL << 1) /* Readable */
#define PTE_W (1UL << 2) /* Writable */
#define PTE_X (1UL << 3) /* Executable */
#define PTE_U (1UL << 4) /* User-mode accessible */
#define PTE_G (1UL << 5) /* Global */
#define PTE_A (1UL << 6) /* Accessed */
#define PTE_D (1UL << 7) /* Dirty */

/* Page table levels */
#define PTE_SHIFT   10UL
#define PA2PTE(pa)  ((((unsigned long)(pa)) >> 12) << PTE_SHIFT)
#define PTE2PA(pte) (((pte) >> PTE_SHIFT) << 12)

#define PTE_FLAGS(pte) ((pte) & 0x3FFUL)

#define NPTE 512 /* Entries per page table */

/* satp CSR fields */
#define SATP_SV39 (8UL << 60)
#define SATP_MODE 0xF000000000000000UL
#define SATP_ASID 0x0FFF000000000000UL
#define SATP_PPN  0x00000FFFFFFFFFFFUL

#define MAKE_SATP(pgd_pa)                                                      \
  (SATP_SV39 | (((unsigned long)(pgd_pa) >> 12) & SATP_PPN))

XDEF_STRUCT(PageTableEntry) { unsigned long raw; };

void vmm_init(void);

#endif /* _ARCH_RISCV_MMU_H */
