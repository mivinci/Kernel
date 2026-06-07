#ifndef _ARCH_RISCV_TIMER_H
#define _ARCH_RISCV_TIMER_H

extern unsigned long mtimer_mmio_base;

/*
 * Timer type — set by FDT (or default ACLINT for QEMU virt).
 *   1 = ACLINT: mtimecmp at base + 8*hartid, mtime at base + 0x7FF8
 *   0 = CLINT:  mtimecmp at base + 0x4000 + 8*hartid, mtime at base + 0xBFF8
 */
extern int timer_is_aclint;

/* QEMU default timebase frequency: 10 MHz */
#define TIMER_INTERVAL 10000000UL

/* mie bit for machine timer interrupt */
#define MIE_MTIE (1 << 7)

static inline unsigned long read_mtime(void) {
  if (timer_is_aclint)
    return *(volatile unsigned long *)(mtimer_mmio_base + 0x7FF8);
  else
    return *(volatile unsigned long *)(mtimer_mmio_base + 0xBFF8);
}

static inline void write_mtimecmp(unsigned long hartid, unsigned long val) {
  if (timer_is_aclint)
    *(volatile unsigned long *)(mtimer_mmio_base + 8 * (hartid)) = val;
  else
    *(volatile unsigned long *)(mtimer_mmio_base + 0x4000 + 8 * (hartid)) = val;
}

void timer_init(void);
void timer_handle(void);

#endif /* _ARCH_RISCV_TIMER_H */
