#include <hal/irq.h>
int
hal_irq_pages(const Hirq *c, u64 pages[HIRQ_WINDOWS])
{
  u64 claim, enable;
  if (!c || !pages || c->kind != HIRQ_PLIC || c->regions != 1 || !c->base[0] ||
      (c->base[0] & PAGE_MASK) || c->bytes[0] < 0x201000 ||
      c->base[0] > ~(u64) 0 - (c->bytes[0] - 1) || c->base[1] || c->bytes[1] || !c->limit ||
      c->limit > 1023 || c->count > HIRQ_SOURCES || (c->count && !c->source) || c->reserved)
    return -1;
  claim = 0x200000UL + (u64) c->context * PAGE_SIZE;
  enable = ALIGN_DOWN(0x2000UL + (u64) c->context * 0x80, PAGE_SIZE);
  if (claim > c->bytes[0] - PAGE_SIZE || enable > c->bytes[0] - PAGE_SIZE)
    return -1;
  pages[0] = c->base[0];
  pages[1] = c->base[0] + claim;
  pages[2] = c->base[0] + enable;
  return 3;
}
