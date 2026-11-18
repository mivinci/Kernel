#include <hal/irq.h>
int
hal_irq_pages(const Hirq *c, u64 pages[HIRQ_WINDOWS])
{
  u32 i;
  if (!c || !pages || c->kind != HIRQ_GIC || c->regions != 2 || c->limit || c->context ||
      c->reserved || c->count > HIRQ_SOURCES || (c->count && !c->source))
    return -1;
  for (i = 0; i < 2; i++)
    if (!c->base[i] || (c->base[i] & PAGE_MASK) || c->bytes[i] < PAGE_SIZE ||
        c->base[i] > ~(u64) 0 - (c->bytes[i] - 1))
      return -1;
  if (c->base[0] <= c->base[1] ? c->base[1] - c->base[0] < c->bytes[0]
                               : c->base[0] - c->base[1] < c->bytes[1])
    return -1;
  pages[0] = c->base[0];
  pages[1] = c->base[1];
  pages[2] = 0;
  return 2;
}
