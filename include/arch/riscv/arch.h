#ifndef _ARCH_RISCV_ARCH_H
#define _ARCH_RISCV_ARCH_H

/*
 * Architecture-dependent definitions for both RV32 and RV64.
 * __riscv_xlen is defined by the compiler: 32 or 64.
 */

#if __riscv_xlen == 64
#define XLEN 64
#else
#define XLEN 32
#endif

/* Size of a register in bytes */
#if XLEN == 64
#define REGSZ 8
#else
#define REGSZ 4
#endif

#endif /* _ARCH_RISCV_ARCH_H */
