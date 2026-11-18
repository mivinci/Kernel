#include <page.h>

#define RESERVED  0
#define FREE      1
#define ALLOCATED 2
#define INNER     3

typedef struct Pgregion Pgregion;
struct Pgregion
{
  u64 base;
  u32 first, pages;
};
static struct
{
  Page    *page;
  Pgregion region[DT_RAM];
  u32      regions, count, ready;
  u32      first[PG_ORDER + 1];
  u64      blocks[PG_ORDER + 1];
  u64      managed, free, allocated, lowwater, committed;
} allocator;

static int
validrange(Dtrange r)
{
  return r.size && !(r.base & PAGE_MASK) && !(r.size & PAGE_MASK) && r.size <= ~(u64) 0 - r.base;
}

static int
intersects(Dtrange a, Dtrange b)
{
  return a.base < b.base + b.size && b.base < a.base + a.size;
}

static int
indexof(u64 pa, u32 *index)
{
  u32 i;
  for (i = 0; i < allocator.regions; i++) {
    const Pgregion *r = &allocator.region[i];
    if (pa >= r->base && (pa - r->base) / PAGE_SIZE < r->pages) {
      *index = r->first + (u32) ((pa - r->base) / PAGE_SIZE);
      return 1;
    }
  }
  return 0;
}

static u64
physical(u32 index)
{
  const Page     *p = &allocator.page[index];
  const Pgregion *r = &allocator.region[p->region];
  return r->base + (u64) (index - r->first) * PAGE_SIZE;
}

static void
insert(u32 index, u32 order)
{
  Page *p = &allocator.page[index];
  u32   first = allocator.first[order];
  p->state = FREE;
  p->order = order;
  p->head = index;
  p->prev = PG_NONE;
  p->next = first;
  p->owner = 0;
  if (first != PG_NONE)
    allocator.page[first].prev = index;
  allocator.first[order] = index;
  allocator.blocks[order]++;
}

static void
removeblock(u32 index)
{
  Page *p = &allocator.page[index];
  if (p->prev == PG_NONE)
    allocator.first[p->order] = p->next;
  else
    allocator.page[p->prev].next = p->next;
  if (p->next != PG_NONE)
    allocator.page[p->next].prev = p->prev;
  allocator.blocks[p->order]--;
  p->state = INNER;
  p->head = PG_NONE;
  p->prev = p->next = PG_NONE;
}

static void
releaseblock(u32 index, u32 order)
{
  u32   other, region = allocator.page[index].region;
  u64   pa = physical(index), buddy;
  Page *b;
  allocator.page[index].state = INNER;
  allocator.page[index].head = PG_NONE;
  /* This head may become the interior of a lower buddy. Ownership belongs
     only to allocated heads, including after coalescing and later splits. */
  allocator.page[index].owner = 0;
  while (order < PG_ORDER) {
    buddy = pa ^ ((u64) PAGE_SIZE << order);
    if (!indexof(buddy, &other))
      break;
    b = &allocator.page[other];
    if (b->region != region || b->state != FREE || b->order != order)
      break;
    removeblock(other);
    if (other < index) {
      index = other;
      pa = buddy;
    }
    order++;
  }
  insert(index, order);
}

int
pgplan(const Dtinfo *info, Dtrange *metadata, u32 *pages)
{
  u32 i, j, best = DT_USABLE;
  u64 count = 0, base = ~(u64) 0, bytes, largest = 0;
  if (!metadata || !pages)
    return PG_BAD;
  metadata->base = metadata->size = 0;
  *pages = 0;
  if (!info || !info->nram || info->nram > DT_RAM || info->nusable > DT_USABLE ||
      info->nreserved > DT_RESERVED)
    return PG_BAD;
  for (i = 0; i < info->nram; i++) {
    if (!validrange(info->ram[i]) || info->ram[i].size / PAGE_SIZE > PG_RAM_PAGES - count ||
        info->ram[i].base + info->ram[i].size > (1UL << 40))
      return PG_BAD;
    for (j = 0; j < i; j++)
      if (intersects(info->ram[i], info->ram[j]))
        return PG_BAD;
    count += info->ram[i].size / PAGE_SIZE;
    base = MIN(base, info->ram[i].base);
  }
  for (i = 0; i < info->nram; i++)
    if (info->ram[i].base + info->ram[i].size - base > KMAP_BYTES)
      return PG_BAD;
  for (i = 0; i < info->nreserved; i++)
    if (!info->reserved[i].size || info->reserved[i].size > ~(u64) 0 - info->reserved[i].base)
      return PG_BAD;
  bytes = ALIGN_UP(count * sizeof(Page), PAGE_SIZE);
  for (i = 0; i < info->nusable; i++) {
    Dtrange r = info->usable[i];
    if (!validrange(r))
      return PG_BAD;
    for (j = 0; j < info->nreserved; j++)
      if (intersects(r, info->reserved[j]))
        return PG_BAD;
    for (j = 0; j < i; j++)
      if (intersects(r, info->usable[j]))
        return PG_BAD;
    for (j = 0; j < info->nram; j++)
      if (r.base >= info->ram[j].base && r.base - info->ram[j].base < info->ram[j].size &&
          r.size <= info->ram[j].size - (r.base - info->ram[j].base))
        break;
    if (j == info->nram)
      return PG_BAD;
    if (r.size >= bytes && r.size > largest) {
      best = i;
      largest = r.size;
    }
  }
  if (best == DT_USABLE)
    return PG_NOMEM;
  metadata->base = info->usable[best].base;
  metadata->size = bytes;
  *pages = (u32) count;
  return 0;
}
int
pginit(const Dtinfo *info, Page *pages, u32 capacity, u64 lowwater)
{
  u32 i, j, k, count = 0, index, order, region = 0;
  u64 total = 0, managed = 0, pa, end, bytes;
  if (allocator.ready && (allocator.allocated || allocator.committed))
    return PG_BUSY;
  if (!pages || !info || !info->nram || info->nram > DT_RAM || !info->nusable ||
      info->nusable > DT_USABLE || info->nreserved > DT_RESERVED ||
      (u64) pages & (__alignof__(Page) - 1))
    return PG_BAD;
  for (i = 0; i < info->nram; i++) {
    if (!validrange(info->ram[i]))
      return PG_BAD;
    for (j = 0; j < i; j++)
      if (intersects(info->ram[i], info->ram[j]))
        return PG_BAD;
    bytes = info->ram[i].size / PAGE_SIZE;
    if (bytes > capacity - total)
      return PG_BAD;
    total += bytes;
  }
  for (i = 0; i < info->nreserved; i++) {
    if (!info->reserved[i].size || info->reserved[i].size > ~(u64) 0 - info->reserved[i].base)
      return PG_BAD;
  }
  for (i = 0; i < info->nusable; i++) {
    Dtrange r = info->usable[i];
    if (!validrange(r))
      return PG_BAD;
    for (j = 0; j < info->nreserved; j++)
      if (intersects(r, info->reserved[j]))
        return PG_BAD;
    for (j = 0; j < i; j++)
      if (intersects(r, info->usable[j]))
        return PG_BAD;
    for (j = 0; j < info->nram; j++) {
      if (r.base >= info->ram[j].base && r.base - info->ram[j].base < info->ram[j].size &&
          r.size <= info->ram[j].size - (r.base - info->ram[j].base))
        break;
    }
    if (j == info->nram)
      return PG_BAD;
    managed += r.size / PAGE_SIZE;
  }
  if (lowwater > managed)
    return PG_BAD;
  /* Validation above is read-only: a failed reinitialization keeps the old pool. */
  allocator.page = pages;
  allocator.regions = info->nram;
  allocator.count = (u32) total;
  allocator.managed = allocator.free = managed;
  allocator.allocated = allocator.committed = 0;
  allocator.lowwater = lowwater;
  for (i = 0; i <= PG_ORDER; i++) {
    allocator.first[i] = PG_NONE;
    allocator.blocks[i] = 0;
  }
  for (i = 0; i < info->nram; i++) {
    allocator.region[i].base = info->ram[i].base;
    allocator.region[i].first = count;
    allocator.region[i].pages = (u32) (info->ram[i].size / PAGE_SIZE);
    for (j = 0; j < allocator.region[i].pages; j++) {
      Page *p = &pages[count++];
      p->state = RESERVED;
      p->order = 0;
      p->head = PG_NONE;
      p->prev = p->next = PG_NONE;
      p->region = i;
      p->owner = 0;
    }
  }
  for (i = 0; i < info->nusable; i++) {
    pa = info->usable[i].base;
    end = pa + info->usable[i].size;
    while (pa < end) {
      indexof(pa, &index);
      region = pages[index].region;
      order = 0;
      while (order < PG_ORDER && !(pa & (((u64) PAGE_SIZE << (order + 1)) - 1)) &&
             ((u64) PAGE_SIZE << (order + 1)) <= end - pa)
        order++;
      for (k = 0; k < (1U << order); k++) {
        pages[index + k].state = INNER;
        pages[index + k].region = region;
      }
      releaseblock(index, order);
      pa += (u64) PAGE_SIZE << order;
    }
  }
  allocator.ready = 1;
  return pgcheck();
}

static int
allocate(u32 order, const Pglim *lim, Pgblk *out, u64 credit)
{
  u32   found, index, candidate, split, right;
  u64   minpa = 0, maxpa = ~(u64) 0, align = PAGE_SIZE, bytes, base, size, chosen, end;
  Page *p;
  if (!allocator.ready || !out || order > PG_ORDER)
    return PG_BAD;
  if (lim) {
    minpa = lim->minpa;
    maxpa = lim->maxpa_exclusive;
    align = lim->align;
  }
  if (!align || align & (align - 1) || minpa >= maxpa)
    return PG_BAD;
  bytes = (u64) PAGE_SIZE << order;
  align = MAX(align, bytes);
  if (credit > allocator.committed ||
      (1UL << order) > allocator.free - allocator.lowwater - (allocator.committed - credit))
    return PG_NOMEM;
  for (found = order; found <= PG_ORDER; found++) {
    for (index = allocator.first[found]; index != PG_NONE; index = allocator.page[index].next) {
      base = physical(index);
      size = (u64) PAGE_SIZE << found;
      end = MIN(base + size, maxpa);
      chosen = MAX(base, minpa);
      if (chosen > ~(u64) 0 - (align - 1))
        continue;
      chosen = ALIGN_UP(chosen, align);
      if (chosen >= end || bytes > end - chosen)
        continue;
      removeblock(index);
      candidate = index + (u32) ((chosen - base) / PAGE_SIZE);
      for (split = found; split > order;) {
        split--;
        right = index + (1U << split);
        if (candidate >= right) {
          insert(index, split);
          index = right;
        } else
          insert(right, split);
      }
      p = &allocator.page[index];
      p->state = ALLOCATED;
      p->order = order;
      p->head = index;
      p->prev = p->next = PG_NONE;
      p->owner = 0;
      allocator.free -= 1UL << order;
      allocator.allocated += 1UL << order;
      out->pa = chosen;
      out->order = order;
      out->reserved = 0;
      return 0;
    }
  }
  return PG_NOMEM;
}

int
pgalloc(u32 order, const Pglim *lim, Pgblk *out)
{
  return allocate(order, lim, out, 0);
}
int
pgreserve(Pgcredit *credit, u64 pages)
{
  if (!allocator.ready || !credit || !pages || credit->pages > allocator.committed)
    return PG_BAD;
  if (pages > allocator.free - allocator.lowwater - allocator.committed)
    return PG_NOMEM;
  credit->pages += pages;
  allocator.committed += pages;
  return 0;
}
int
pgcancel(Pgcredit *credit, u64 pages)
{
  if (!allocator.ready || !credit || !pages || pages > credit->pages ||
      credit->pages > allocator.committed)
    return PG_BAD;
  credit->pages -= pages;
  allocator.committed -= pages;
  return 0;
}
int
pgredeem(Pgcredit *credit, Pgblk *out)
{
  int error;
  if (!allocator.ready || !credit || !credit->pages || credit->pages > allocator.committed)
    return PG_BAD;
  error = allocate(0, NULL, out, 1);
  if (!error) {
    credit->pages--;
    allocator.committed--;
  }
  return error;
}
u64
pgreserved(void)
{
  return allocator.committed;
}

int
pgclaim(Pgblk blk, u64 owner)
{
  u32   index;
  Page *p;
  if (!owner || !allocator.ready || blk.reserved || blk.order > PG_ORDER ||
      blk.pa & (((u64) PAGE_SIZE << blk.order) - 1) || !indexof(blk.pa, &index))
    return PG_BAD;
  p = &allocator.page[index];
  if (p->state != ALLOCATED || p->order != blk.order || p->head != index || p->owner)
    return PG_BAD;
  p->owner = owner;
  return 0;
}

int
pgowned(Pgblk blk, u64 owner)
{
  u32   index;
  Page *p;
  if (!owner || !allocator.ready || blk.reserved || blk.order > PG_ORDER ||
      blk.pa & (((u64) PAGE_SIZE << blk.order) - 1) || !indexof(blk.pa, &index))
    return PG_BAD;
  p = &allocator.page[index];
  return p->state == ALLOCATED && p->order == blk.order && p->head == index && p->owner == owner
             ? 0
             : PG_BAD;
}
int
pgtransfer(Pgblk blk, u64 oldowner, u64 newowner)
{
  u32 index;
  if (!newowner || pgowned(blk, oldowner))
    return PG_BAD;
  indexof(blk.pa, &index);
  allocator.page[index].owner = newowner;
  return 0;
}

int
pgfree(Pgblk blk)
{
  return pgrelease(blk, 0);
}

int
pgrelease(Pgblk blk, u64 owner)
{
  u32   index, order = blk.order;
  Page *p;
  if (!allocator.ready || blk.reserved || order > PG_ORDER ||
      blk.pa & (((u64) PAGE_SIZE << order) - 1) || !indexof(blk.pa, &index))
    return PG_BAD;
  p = &allocator.page[index];
  if (p->state != ALLOCATED || p->order != order || p->head != index || p->owner != owner)
    return PG_BAD;
  allocator.free += 1UL << order;
  allocator.allocated -= 1UL << order;
  releaseblock(index, order);
  return 0;
}

void
pgstat(Pgstat *out)
{
  u32 i;
  out->total = allocator.count;
  out->managed = allocator.managed;
  out->free = allocator.free;
  out->allocated = allocator.allocated;
  out->protected = allocator.lowwater;
  for (i = 0; i <= PG_ORDER; i++)
    out->blocks[i] = allocator.blocks[i];
}

int
pgcheck(void)
{
  u32             i, j, order, count, previous, index, buddy;
  u64             free = 0, allocated = 0, scanned = 0, pa;
  Page           *p;
  const Pgregion *r;
  if (!allocator.ready || allocator.lowwater > allocator.free ||
      allocator.committed > allocator.free - allocator.lowwater)
    return PG_BAD;
  for (order = 0; order <= PG_ORDER; order++) {
    previous = PG_NONE;
    count = 0;
    for (index = allocator.first[order]; index != PG_NONE; index = p->next) {
      if (index >= allocator.count || count++ >= allocator.count)
        return PG_BAD;
      p = &allocator.page[index];
      if (p->state != FREE || p->order != order || p->head != index || p->prev != previous)
        return PG_BAD;
      previous = index;
    }
    if (count != allocator.blocks[order])
      return PG_BAD;
    free += (u64) count << order;
  }
  for (i = 0; i < allocator.count;) {
    p = &allocator.page[i];
    if (p->state == RESERVED) {
      if (p->owner)
        return PG_BAD;
      i++;
      continue;
    }
    if (p->state != FREE && p->state != ALLOCATED)
      return PG_BAD;
    order = p->order;
    if (order > PG_ORDER || p->region >= allocator.regions || p->head != i)
      return PG_BAD;
    r = &allocator.region[p->region];
    pa = physical(i);
    if (pa & (((u64) PAGE_SIZE << order) - 1) || i + (1U << order) > r->first + r->pages)
      return PG_BAD;
    for (j = 1; j < (1U << order); j++)
      if (allocator.page[i + j].state != INNER || allocator.page[i + j].region != p->region ||
          allocator.page[i + j].owner)
        return PG_BAD;
    if (p->state == ALLOCATED)
      allocated += 1UL << order;
    else {
      if (p->owner)
        return PG_BAD;
      scanned += 1UL << order;
      if (order < PG_ORDER && indexof(pa ^ ((u64) PAGE_SIZE << order), &buddy) &&
          allocator.page[buddy].region == p->region && allocator.page[buddy].state == FREE &&
          allocator.page[buddy].order == order)
        return PG_BAD;
    }
    i += 1U << order;
  }
  if (scanned != free || free != allocator.free || allocated != allocator.allocated ||
      free + allocated != allocator.managed)
    return PG_BAD;
  return 0;
}

void
pgrelocate(u64 delta)
{
  allocator.page = (Page *) ((u64) allocator.page + delta);
}
