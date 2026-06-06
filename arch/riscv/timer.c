#include <kernel.h>
#include <arch/riscv/csr.h>
#include <arch/riscv/timer.h>

static unsigned long ticks = 0;

void timer_init(void) {
  unsigned long hartid = csr_read(mhartid);

  /* Set first timer interrupt: mtimecmp = mtime + interval */
  write_mtimecmp(hartid, read_mtime() + TIMER_INTERVAL);

  /* Enable machine timer interrupt in mie */
  csr_write(mie, csr_read(mie) | MIE_MTIE);

  printk("[timer] hart=%d interval=%dms\n", hartid,
         TIMER_INTERVAL / 10000);
}

void timer_handle(void) {
  unsigned long hartid = csr_read(mhartid);

  /* Ack the interrupt by setting next timeout */
  write_mtimecmp(hartid, read_mtime() + TIMER_INTERVAL);

  ticks++;
  if (ticks % 5 == 0)
    printk("[timer] tick=%d (hart %d)\n", ticks, hartid);
}
