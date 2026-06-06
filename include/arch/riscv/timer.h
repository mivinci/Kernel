#ifndef _ARCH_RISCV_TIMER_H
#define _ARCH_RISCV_TIMER_H

/* ACLINT MTIMER base address for QEMU RISC-V virt machine */
#define MTIMER_BASE 0x2004000UL

/*
 * ACLINT MTIMER register layout:
 *   mtimecmp[hartid] at MTIMER_BASE + 8 * hartid
 *   mtime             at MTIMER_BASE + 0x7FF8
 *
 * QEMU default timebase frequency: 10 MHz (10,000,000 Hz)
 */
#define MTIMECMP(hartid) (MTIMER_BASE + 8 * (hartid))
#define MTIME            (MTIMER_BASE + 0x7FF8)

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
