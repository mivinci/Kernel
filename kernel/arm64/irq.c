#include <hal/irq.h>
#include <hal/clock.h>
static u64 distributor, cpu;
static u32 timerirq;
static void
write32(u64 base, u32 offset, u32 value)
{
  *(volatile u32 *) (base + offset) = value;
}
static u32
read32(u64 base, u32 offset)
{
  return *(volatile u32 *) (base + offset);
}
int
hal_timer_init(const Htimer *timer)
{
  u32 i, lines;
  if (!timer || !timer->distributor || !timer->cpu || timer->count != 4 || timer->irqs[2] < 16 ||
      timer->irqs[2] >= 32 || ((timer->flags[2] & 15) != 4 && (timer->flags[2] & 15) != 8) ||
      timer->reserved)
    return -1;
  distributor = timer->distributor;
  cpu = timer->cpu;
  timerirq = timer->irqs[2];
  __asm__ __volatile__("msr cntv_ctl_el0, xzr; isb" ::: "memory");
  write32(distributor, 0, 0);
  write32(cpu, 0, 0);
  i = read32(distributor, 4);
  if (i & (1U << 10))
    return -1; /* Current boot profile has no Security Extensions. */
  lines = ((i & 31) + 1) * 32;
  for (i = 0; i < lines / 32; i++) {
    write32(distributor, 0x180 + 4 * i, 0xffffffffU);
    write32(distributor, 0x280 + 4 * i, 0xffffffffU);
    write32(distributor, 0x80 + 4 * i, 0);
  }
  /* PPI configuration is banked; the virtual timer is level sensitive. */
  i = read32(distributor, 0xc04);
  i &= ~(3U << (2 * (timerirq - 16)));
  write32(distributor, 0xc04, i);
  *(volatile u8 *) (distributor + 0x400 + timerirq) = 0x80;
  write32(cpu, 4, 0xff);
  write32(cpu, 8, 0);
  write32(distributor, 0x100, 1U << timerirq);
  /* Group 0 routed as IRQ (FIQEn=0), matching both enable registers. */
  write32(distributor, 0, 1);
  write32(cpu, 0, 1);
  __asm__ __volatile__("dsb sy; isb" ::: "memory");
  return 0;
}
void
hal_timer_set(u64 ticks)
{
  u64 enable = 1;
  __asm__ __volatile__("msr cntv_cval_el0, %0; msr cntv_ctl_el0, %1; isb" ::"r"(ticks), "r"(enable)
                       : "memory");
}
int
hal_timer_ack(void)
{
  u32 token = read32(cpu, 0xc), irq = token & 1023;
  if (irq >= 1020)
    return 0;
  if (irq == timerirq)
    __asm__ __volatile__("msr cntv_ctl_el0, xzr; isb" ::: "memory");
  else
    write32(distributor, 0x180 + 4 * (irq / 32), 1U << (irq % 32));
  __asm__ __volatile__("dsb sy" ::: "memory");
  write32(cpu, 0x10, token);
  __asm__ __volatile__("dsb sy; isb" ::: "memory");
  return irq == timerirq ? 1 : -1;
}
u64
hal_irq_save(void)
{
  u64 state;
  __asm__ __volatile__("mrs %0, daif; msr daifset, #2" : "=r"(state)::"memory");
  return state & 128;
}
void
hal_irq_restore(u64 state)
{
  if (state & 128)
    __asm__ __volatile__("msr daifset, #2" ::: "memory");
  else
    __asm__ __volatile__("msr daifclr, #2" ::: "memory");
}
