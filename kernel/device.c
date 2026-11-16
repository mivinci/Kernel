#include <device.h>
#include <diag.h>
static Device windows[DEVWINDOWS];
static u32    initialized;
static Device *
find(u32 id)
{
  return initialized && id < DEVWINDOWS && windows[id].actual.size ? &windows[id] : NULL;
}
static int
sameacct(Acctid a, Acctid b)
{
  return a.generation == b.generation && a.index == b.index && !a.reserved && !b.reserved;
}
static int
overlaps(u64 a, u64 bytes, const Dtrange *range)
{
  return a < range->base + range->size && range->base < a + bytes;
}
int
devinit(const Dtinfo *info)
{
  u32 i, j, k, l;
  u64 start, end, bytes;
  if (initialized)
    return DEV_BAD;
  if (!info || info->ndev > DT_DEVICES || info->nram > DT_RAM || info->nreserved > DT_RESERVED)
    return DEV_BAD;
  for (i = 0; i < info->nram; i++)
    if (!info->ram[i].size || info->ram[i].size > ~(u64) 0 - info->ram[i].base)
      return DEV_BAD;
  for (i = 0; i < info->nreserved; i++)
    if (!info->reserved[i].size || info->reserved[i].size > ~(u64) 0 - info->reserved[i].base)
      return DEV_BAD;
  for (i = 0; i < info->ndev; i++) {
    const Dtdev *d = &info->dev[i];
    if (d->nreg > DT_REGS || d->mappable & ~((1U << d->nreg) - 1) || d->reserved ||
        (d->protected && d->mappable))
      return DEV_BAD;
    for (j = 0; j < d->nreg; j++) {
      if (!d->reg[j].size || d->reg[j].size > ~(u64) 0 - d->reg[j].base)
        return DEV_BAD;
      end = d->reg[j].base + d->reg[j].size;
      if (end > ~(u64) 0 - PAGE_MASK)
        return DEV_BAD;
    }
  }
  for (i = 0; i < info->ndev; i++) {
    const Dtdev *d = &info->dev[i];
    for (j = 0; j < d->nreg; j++) {
      end = d->reg[j].base + d->reg[j].size;
      if (!(d->mappable & (1U << j)))
        continue;
      if (d->kind < DT_UART16550 || d->kind > DT_VIRTIO || d->protected)
        return DEV_DENIED;
      start = ALIGN_DOWN(d->reg[j].base, PAGE_SIZE);
      bytes = ALIGN_UP(end, PAGE_SIZE) - start;
      for (k = 0; k < info->nram; k++)
        if (overlaps(start, bytes, &info->ram[k]))
          return DEV_DENIED;
      for (k = 0; k < info->nreserved; k++)
        if (overlaps(start, bytes, &info->reserved[k]))
          return DEV_DENIED;
      for (k = 0; k < info->ndev; k++)
        for (l = 0; l < info->dev[k].nreg; l++)
          if ((i != k || j != l) && overlaps(start, bytes, &info->dev[k].reg[l]))
            return DEV_DENIED;
    }
  }
  for (i = 0; i < info->ndev; i++)
    for (j = 0; j < info->dev[i].nreg; j++) {
      Device *window = &windows[i * DT_REGS + j];
      window->actual.base = info->dev[i].reg[j].base;
      window->actual.size = info->dev[i].reg[j].size;
      window->pages.base = ALIGN_DOWN(window->actual.base, PAGE_SIZE);
      window->pages.size =
          ALIGN_UP(window->actual.base + window->actual.size, PAGE_SIZE) - window->pages.base;
      window->kind = info->dev[i].kind;
      window->allowed = (info->dev[i].mappable >> j) & 1;
    }
  initialized = 1;
  return 0;
}
int
devwindow(u32 device, u32 region, u32 *out)
{
  Device *window;
  u32     id;
  if (!out)
    return DEV_BAD;
  *out = 0;
  if (device >= DT_DEVICES || region >= DT_REGS)
    return DEV_BAD;
  id = device * DT_REGS + region;
  window = find(id);
  if (!window)
    return DEV_BAD;
  if (!window->allowed)
    return DEV_DENIED;
  *out = id;
  return 0;
}
int
devstat(u32 id, Device *out)
{
  Device *window = find(id);
  if (!window || !out)
    return DEV_BAD;
  out->actual.base = window->actual.base;
  out->actual.size = window->actual.size;
  out->pages.base = window->pages.base;
  out->pages.size = window->pages.size;
  out->owner.generation = window->owner.generation;
  out->owner.index = window->owner.index;
  out->owner.reserved = window->owner.reserved;
  out->refs = window->refs;
  out->mappings = window->mappings;
  out->kind = window->kind;
  out->allowed = window->allowed;
  return 0;
}
int
devhold(u32 id)
{
  Device *window = find(id);
  if (!window || !window->allowed)
    return DEV_DENIED;
  if (window->refs == 0xffffffffU)
    return DEV_LIMIT;
  window->refs++;
  return 0;
}
int
devdrop(u32 id)
{
  Device *window = find(id);
  if (!window || !window->refs)
    return DEV_BAD;
  window->refs--;
  if (!window->refs && window->mappings)
    panic("device window references");
  return 0;
}
int
devpage(u32 id, u32 page, u64 *out)
{
  Device *window = find(id);
  if (!window || !window->refs || !out || (u64) page >= window->pages.size / PAGE_SIZE)
    return DEV_BAD;
  *out = window->pages.base + (u64) page * PAGE_SIZE;
  return 0;
}
int
devmap(u32 id, Acctid acct)
{
  Device *window = find(id);
  Acct    owner;
  int     error;
  if (!window || !window->allowed || !window->refs || acctstat(acct, &owner) ||
      owner.state != AC_ACTIVE)
    return DEV_BAD;
  if (window->mappings && !sameacct(window->owner, acct))
    return DEV_DENIED;
  if (window->mappings == 0xffffffffU)
    return DEV_LIMIT;
  if (!window->mappings) {
    error = accthold(acct);
    if (error)
      return error;
    window->owner = acct;
  }
  window->mappings++;
  return 0;
}
int
devunmap(u32 id, Acctid acct)
{
  Device *window = find(id);
  if (!window || !window->mappings || !sameacct(window->owner, acct))
    return DEV_BAD;
  window->mappings--;
  if (!window->mappings) {
    window->owner.generation = 0;
    window->owner.index = window->owner.reserved = 0;
    if (acctdrop(acct))
      panic("device account lease");
  }
  return 0;
}
int
devcheck(void)
{
  Device *window;
  Acct    owner;
  u32     i;
  for (i = 0; i < DEVWINDOWS; i++) {
    window = &windows[i];
    if (!window->actual.size)
      continue;
    if (window->mappings) {
      if (!window->allowed || !window->refs || acctstat(window->owner, &owner))
        return DEV_BAD;
    } else if (window->owner.generation || window->owner.index || window->owner.reserved)
      return DEV_BAD;
  }
  return 0;
}
