#include <hal/irq.h>
#include <hal/clock.h>
#include <diag.h>
static u64 distributor, cpu;
static u32 timerirq, lines;
static u32 allowed[32];
static u64 active;
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
hal_irq_init(const Hirq *controller, const u64 mapped[HIRQ_WINDOWS])
{
  u64 physical[HIRQ_WINDOWS], dist, interface;
  u32 i, j, count, value, number;
  u8  target;
  if (distributor || !mapped || hal_irq_pages(controller, physical) != 2 || !mapped[0] ||
      !mapped[1] || ((mapped[0] | mapped[1]) & PAGE_MASK))
    return -1;
  dist = mapped[0];
  interface = mapped[1];
  value = read32(dist, 4);
  if (value & (1U << 10))
    return -1;
  count = ((value & 31) + 1) * 32;
  if (count > 1020)
    count = 1020;
  target = 0;
  /* A uniprocessor GIC's target registers are RAZ/WI. */
  if (value & 0xe0) {
    target = *(volatile u8 *) (dist + 0x800);
    if (!target || (target & (target - 1)))
      return -1;
  }
  for (i = 0; i < controller->count; i++) {
    const Hirqsource *source = &controller->source[i];
    if (source->number < 32 || source->number >= count ||
        (source->flags != 1 && source->flags != 4))
      return -1;
    for (j = 0; j < i; j++)
      if (controller->source[j].number == source->number &&
          controller->source[j].flags != source->flags)
        return -1;
  }
  write32(dist, 0, 0);
  write32(interface, 0, 0);
  for (i = 0; i < (count + 31) / 32; i++) {
    write32(dist, 0x180 + 4 * i, 0xffffffffU);
    write32(dist, 0x280 + 4 * i, 0xffffffffU);
    write32(dist, 0x380 + 4 * i, 0xffffffffU);
    write32(dist, 0x80 + 4 * i, 0);
  }
  for (i = 0; i < 4; i++)
    write32(dist, 0xf10 + 4 * i, 0xffffffffU);
  for (i = 0; i < controller->count; i++) {
    const Hirqsource *source = &controller->source[i];
    number = source->number;
    value = read32(dist, 0xc00 + 4 * (number / 16));
    value &= ~(3U << (2 * (number % 16)));
    if (source->flags == 1)
      value |= 2U << (2 * (number % 16));
    write32(dist, 0xc00 + 4 * (number / 16), value);
    *(volatile u8 *) (dist + 0x400 + number) = 0x80;
    if (target)
      *(volatile u8 *) (dist + 0x800 + number) = target;
    allowed[number / 32] |= 1U << (number % 32);
  }
  write32(interface, 4, 0xff);
  write32(interface, 8, 0);
  /* Existing non-secure QEMU profile routes Group 0 as IRQ, with EOI mode 0. */
  write32(dist, 0, 1);
  write32(interface, 0, 1);
  __asm__ __volatile__("dsb sy; isb" ::: "memory");
  distributor = dist;
  cpu = interface;
  lines = count;
  return 0;
}
int
hal_timer_init(const Htimer *timer)
{
  u32 value;
  if (!distributor || !cpu || !timer || timerirq || timer->count != 4 || timer->irqs[2] < 16 ||
      timer->irqs[2] >= 32 || ((timer->flags[2] & 15) != 4 && (timer->flags[2] & 15) != 8) ||
      timer->reserved)
    return -1;
  timerirq = timer->irqs[2];
  __asm__ __volatile__("msr cntv_ctl_el0, xzr; isb" ::: "memory");
  value = read32(distributor, 0xc04);
  value &= ~(3U << (2 * (timerirq - 16)));
  write32(distributor, 0xc04, value);
  *(volatile u8 *) (distributor + 0x400 + timerirq) = 0x80;
  allowed[timerirq / 32] |= 1U << (timerirq % 32);
  hal_irq_unmask(timerirq);
  return 0;
}
u64
hal_irq_claim(void)
{
  u32 raw;
  if (!cpu || active)
    panic("IRQ claim order");
  raw = read32(cpu, 0xc);
  if ((raw & 1023) >= 1020)
    return 0;
  active = (u64) raw + 1;
  return active;
}
u32
hal_irq_number(u64 token)
{
  return token ? (u32) ((token - 1) & 1023) : 0xffffffffU;
}
void
hal_irq_mask(u32 number)
{
  if (!distributor || number < 16 || number >= lines)
    panic("IRQ mask number");
  write32(distributor, 0x180 + 4 * (number / 32), 1U << (number % 32));
  __asm__ __volatile__("dsb sy; isb" ::: "memory");
}
void
hal_irq_complete(u64 token)
{
  if (!cpu || !token || token != active)
    panic("IRQ completion order");
  __asm__ __volatile__("dsb sy" ::: "memory");
  write32(cpu, 0x10, (u32) (token - 1));
  __asm__ __volatile__("dsb sy; isb" ::: "memory");
  active = 0;
}
void
hal_irq_unmask(u32 number)
{
  if (!distributor || number >= lines || !(allowed[number / 32] & (1U << (number % 32))))
    panic("IRQ unmask number");
  write32(distributor, 0x100 + 4 * (number / 32), 1U << (number % 32));
  __asm__ __volatile__("dsb sy; isb" ::: "memory");
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
  u64 token = hal_irq_claim();
  u32 number;
  if (!token)
    return 0;
  number = hal_irq_number(token);
  if (number == timerirq)
    __asm__ __volatile__("msr cntv_ctl_el0, xzr; isb" ::: "memory");
  else
    hal_irq_mask(number);
  hal_irq_complete(token);
  return number == timerirq ? 1 : -1;
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
