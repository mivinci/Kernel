#include "chip.h"
#include <hal/irq.h>
#include <vm.h>
#include <diag.h>
static u32 saved;
void
chipcpu(const Dtinfo *info, u64 cpu)
{
  UNUSED(info);
  UNUSED(cpu);
}
void
chipbegin(void)
{
  saved = *(volatile u32 *) (KIO + 0x38);
  *(volatile u32 *) (KIO + 0x38) = 0;
  *(volatile u32 *) (KIO + 0x44) = 0x7ff;
  *(volatile u32 *) (KIO + 0) = '\n';
  *(volatile u32 *) (KIO + 0x38) = 1U << 5;
  __asm__ __volatile__("dsb sy; isb" ::: "memory");
}
void
chipend(void)
{
  *(volatile u32 *) (KIO + 0x38) = saved;
  *(volatile u32 *) (KIO + 0x44) = 0x7ff;
  __asm__ __volatile__("dsb sy; isb" ::: "memory");
}
u32
chipspecial(void)
{
  u32 i;
  u64 token;
  *(volatile u32 *) (KIRQ_DIST + 0xf00) = 2U << 24;
  __asm__ __volatile__("dsb sy; isb" ::: "memory");
  for (i = 0; i < 100000; i++) {
    token = hal_irq_claim();
    if (!token)
      continue;
    if (hal_irq_number(token))
      panic("SGI zero identity");
    hal_irq_complete(token);
    if (hal_irq_claim())
      panic("SGI zero completion");
    return 1;
  }
  panic("SGI zero missing");
}
u32
chipmasked(void)
{
  u64 value;
  __asm__ __volatile__("mrs %0, daif" : "=r"(value));
  return (value & 128) != 0;
}

u32
chipmaskcheck(u32 number)
{
  return (*(volatile u32 *) (KIRQ_DIST + 0x100 + 4 * (number / 32)) & (1U << (number % 32))) == 0;
}
