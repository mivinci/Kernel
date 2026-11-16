#include <hal/irq.h>
int
hal_irq_pages(const Hirq *c, u64 pages[HIRQ_WINDOWS])
{
  if (!c || !pages || c->kind != HIRQ_PLIC || c->regions != 1 || !c->base[0] ||
      (c->base[0] & PAGE_MASK) || c->bytes[0] < 0x202000 ||
      c->base[0] > ~(u64) 0 - (c->bytes[0] - 1) || c->base[1] || c->bytes[1] || !c->limit ||
      c->limit > 1023 || c->count > HIRQ_SOURCES || (c->count && !c->source) ||
      (c->parent[0] != 11 && c->parent[0] != 0xffffffffU) || c->parent[1] != 9)
    return -1;
  pages[0] = c->base[0];
  pages[1] = c->base[0] + 0x201000;
  pages[2] = c->base[0] + 0x2000;
  return 3;
}
