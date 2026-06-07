#include <arch/riscv/csr.h>
#include <arch/riscv/timer.h>
#include <kernel.h>
#include <proc.h>

static unsigned long ticks = 0;

void timer_init(void) {
  unsigned long hartid = csr_read(mhartid);

  write_mtimecmp(hartid, read_mtime() + TIMER_INTERVAL);
  csr_write(mie, csr_read(mie) | MIE_MTIE);

  printk("[timer] hart=%d interval=%dms\n", hartid, TIMER_INTERVAL / 10000);
}

void timer_handle(void) {
  unsigned long hartid = csr_read(mhartid);

  write_mtimecmp(hartid, read_mtime() + TIMER_INTERVAL);

  ticks++;
  if (ticks % 5 == 0) printk("[timer] tick=%d (hart %d)\n", ticks, hartid);

  /* Preempt current process if one is running */
  sched_tick();
}
