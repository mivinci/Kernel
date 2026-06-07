#ifndef _ARCH_RISCV_TIMER_H
#define _ARCH_RISCV_TIMER_H

/* ACLINT MTIMER base — runtime variable, default QEMU virt */
extern unsigned long mtimer_mmio_base;

/*
 * ACLINT MTIMER register layout:
 *   mtimecmp[hartid] at base + 8 * hartid
 *   mtime             at base + 0x7FF8
 *
 * QEMU default timebase frequency: 10 MHz (10,000,000 Hz)
 */
#define MTIMECMP(hartid) (mtimer_mmio_base + 8 * (hartid))
#define MTIME            (mtimer_mmio_base + 0x7FF8)

/* Timer interval: 1 second at 10 MHz = 10,000,000 ticks */
#define TIMER_INTERVAL 10000000UL

/* mie bit for machine timer interrupt */
#define MIE_MTIE (1 << 7)

/* Volatile 64-bit MMIO reads/writes for CLINT registers */
static inline unsigned long read_mtime(void) {
  return *(volatile unsigned long *)MTIME;
}

static inline void write_mtimecmp(unsigned long hartid, unsigned long val) {
  *(volatile unsigned long *)MTIMECMP(hartid) = val;
}

void timer_init(void);
void timer_handle(void);

#endif /* _ARCH_RISCV_TIMER_H */
