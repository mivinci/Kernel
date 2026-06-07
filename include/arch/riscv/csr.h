#ifndef _ARCH_RISCV_CSR_H
#define _ARCH_RISCV_CSR_H

/* CSR read/write macros for RISC-V machine mode */

#define csr_read(csr)                                       \
  ({                                                        \
    unsigned long __val;                                    \
    __asm__ __volatile__("csrr %0, " #csr : "=r"(__val) :); \
    __val;                                                  \
  })

#define csr_write(csr, val) ({ __asm__ __volatile__("csrw " #csr ", %0" : : "rK"(val)); })

#define csr_set(csr, bits)   ({ __asm__ __volatile__("csrs " #csr ", %0" : : "rK"(bits)); })
#define csr_clear(csr, bits) ({ __asm__ __volatile__("csrc " #csr ", %0" : : "rK"(bits)); })

#endif /* _ARCH_RISCV_CSR_H */
