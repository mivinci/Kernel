#ifndef _ARCH_RISCV_CSR_H
#define _ARCH_RISCV_CSR_H

/* CSR read/write macros for RISC-V machine mode */

#define csr_read(csr)                                    \
  ({                                                     \
    unsigned long __val;                                 \
    __asm__ __volatile__("csrr %0, " #csr : "=r"(__val) :); \
    __val;                                               \
  })

#define csr_write(csr, val)                              \
  ({                                                     \
    __asm__ __volatile__("csrw " #csr ", %0" : : "rK"(val)); \
  })

#endif /* _ARCH_RISCV_CSR_H */
