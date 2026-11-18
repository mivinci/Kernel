#include "chip.h"
#include <vm.h>
#include <diag.h>
#include <irq.h>
static u8  saved;
static u64 enableoffset;
void
chipcpu(const Dtinfo *info, u64 cpu)
{
  Hirq       controller;
  Hirqsource sources[HIRQ_SOURCES];
  if (irqdescribe(info, cpu, &controller, sources, HIRQ_SOURCES) || controller.kind != HIRQ_PLIC)
    panic("test PLIC context");
  enableoffset = (0x2000UL + (u64) controller.context * 0x80) & PAGE_MASK;
}
void
chipbegin(void)
{
  if (*(volatile u8 *) (KIO + 3) & 0x80)
    panic("UART divisor mode");
  saved = *(volatile u8 *) (KIO + 1);
  *(volatile u8 *) (KIO + 1) = 0;
  *(volatile u8 *) (KIO + 1) = 2;
  __asm__ __volatile__("fence iorw, iorw" ::: "memory");
}
void
chipend(void)
{
  *(volatile u8 *) (KIO + 1) = saved;
  __asm__ __volatile__("fence iorw, iorw" ::: "memory");
}
u32
chipspecial(void)
{
  return 0; /* PLIC source zero does not exist. */
}
u32
chipmasked(void)
{
  u64 value;
  __asm__ __volatile__("csrr %0, sstatus" : "=r"(value));
  return (value & 2) == 0;
}

u32
chipmaskcheck(u32 number)
{
  return *(volatile u32 *) (KIRQ_DIST + 4 * number) == 0 &&
         (*(volatile u32 *) (KIRQ_ENABLE + enableoffset + 4 * (number / 32)) &
          (1U << (number % 32))) != 0;
}
