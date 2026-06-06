#ifndef _ARCH_RISCV_PLIC_H
#define _ARCH_RISCV_PLIC_H

#define PLIC_BASE 0x0c000000UL

/* PLIC register offsets */
#define PLIC_PRIORITY                0x000000
#define PLIC_PENDING                 0x001000
#define PLIC_ENABLE(hartid, mode)    (0x002000 + ((hartid) * 2 + (mode)) * 0x80)
#define PLIC_THRESHOLD(hartid, mode) (0x200000 + ((hartid) * 2 + (mode)) * 0x1000)
#define PLIC_CLAIM(hartid, mode)     (0x200004 + ((hartid) * 2 + (mode)) * 0x1000)

/* Context modes */
#define PLIC_MODE_M 0
#define PLIC_MODE_S 1

/* Interrupt source numbers */
#define UART0_IRQ 10

/* mie bit for machine external interrupt */
#define MIE_MEIE (1 << 11)

/* Volatile MMIO helpers (PLIC registers are 32-bit) */
static inline void plic_write(unsigned long reg, unsigned int val) {
  *(volatile unsigned int *)(PLIC_BASE + reg) = val;
}

static inline unsigned int plic_read(unsigned long reg) {
  return *(volatile unsigned int *)(PLIC_BASE + reg);
}

void plic_init(void);
void plic_handle(void);

#endif /* _ARCH_RISCV_PLIC_H */
