/* Retained recovery source. No ordinary allocator ownership or user authority. */
#include <bootstore.h>
#include <abi/boot.h>
#include <vm.h>

static Bootstore store;
static u64       edges[2];

static int
overlap(u64 a, u64 n, u64 b, u64 m)
{
  return n && m && a < b + m && b < a + n;
}

static void
copyedge(u64 target, u64 original, u64 first, u64 end)
{
  volatile u8 *output = kaddr(target);
  const u8    *input = kaddr(original);
  u64          i;
  for (i = 0; i < PAGE_SIZE; i++)
    output[i] = original + i >= first && original + i < end ? input[i] : 0;
}

int
bootstoreprepare(Dtinfo *info)
{
  Dtrange saved[DT_USABLE], replaced[2];
  u64     selected[2], base, end, pages, copies, i, j, at, found = 0;
  u32     nreserved, nusable, covered = 0;
  int     error;
  if (!info || info->nram > DT_RAM || info->nreserved > DT_RESERVED || info->nusable > DT_USABLE)
    return BOOTSTORE_BAD;
  if (store.bytes)
    return BOOTSTORE_BUSY;
  if (!info->initrd.size)
    return 0;
  if (info->initrd.base > ~(u64) PAGE_MASK ||
      info->initrd.size > ~(u64) PAGE_MASK - info->initrd.base || info->bootedge[0].base ||
      info->bootedge[0].size || info->bootedge[1].base || info->bootedge[1].size)
    return BOOTSTORE_BAD;
  base = ALIGN_DOWN(info->initrd.base, PAGE_SIZE);
  end = info->initrd.base + info->initrd.size;
  pages = (ALIGN_UP(end, PAGE_SIZE) - base) / PAGE_SIZE;
  if (pages > (BOOT_PACKAGE_HI - BOOT_PACKAGE_LO) / PAGE_SIZE)
    return BOOTSTORE_LIMIT;
  for (i = 0; i < info->nram; i++) {
    const Dtrange *r = &info->ram[i];
    if (!r->size || r->size > ~(u64) 0 - r->base)
      return BOOTSTORE_BAD;
    if (base >= r->base && base - r->base < r->size &&
        pages * PAGE_SIZE <= r->size - (base - r->base))
      covered = 1;
  }
  if (!covered)
    return BOOTSTORE_BAD;
  covered = 0;
  for (i = 0; i < info->nreserved; i++) {
    const Dtrange *r = &info->reserved[i];
    if (!r->size || r->size > ~(u64) 0 - r->base)
      return BOOTSTORE_BAD;
    if (r->base == info->initrd.base && r->size == info->initrd.size)
      covered++;
    else if (overlap(base, pages * PAGE_SIZE, r->base, r->size))
      return BOOTSTORE_BAD;
  }
  if (covered != 1)
    return BOOTSTORE_BAD;
  copies = !!(info->initrd.base & PAGE_MASK);
  if ((end & PAGE_MASK) && (!copies || pages != 1))
    copies++;
  if (copies > DT_RESERVED - info->nreserved)
    return BOOTSTORE_FULL;
  /* Select at most two complete free pages without publishing reservations. */
  for (i = 0; i < info->nusable && found < copies; i++) {
    const Dtrange *r = &info->usable[i];
    if (!r->size || (r->base | r->size) & PAGE_MASK || r->size > ~(u64) 0 - r->base)
      return BOOTSTORE_BAD;
    for (at = r->base; at < r->base + r->size && found < copies; at += PAGE_SIZE) {
      covered = 0;
      for (j = 0; j < info->nram; j++)
        if (at >= info->ram[j].base && at - info->ram[j].base < info->ram[j].size &&
            PAGE_SIZE <= info->ram[j].size - (at - info->ram[j].base))
          covered = 1;
      if (!covered)
        return BOOTSTORE_BAD;
      for (j = 0; j < info->nreserved; j++)
        if (overlap(at, PAGE_SIZE, info->reserved[j].base, info->reserved[j].size))
          return BOOTSTORE_BAD;
      if (found && at == selected[0])
        return BOOTSTORE_BAD;
      selected[found++] = at;
    }
  }
  if (found != copies)
    return BOOTSTORE_NOMEM;
  nreserved = info->nreserved;
  nusable = info->nusable;
  for (i = 0; i < copies; i++) {
    replaced[i].base = info->reserved[nreserved + i].base;
    replaced[i].size = info->reserved[nreserved + i].size;
  }
  for (i = 0; i < DT_USABLE; i++) {
    saved[i].base = info->usable[i].base;
    saved[i].size = info->usable[i].size;
  }
  for (i = 0; i < copies; i++)
    if (dtreserve(info, selected[i], PAGE_SIZE))
      break;
  error = i != copies || dtusable(info);
  if (error) {
    for (i = 0; i < copies; i++) {
      info->reserved[nreserved + i].base = replaced[i].base;
      info->reserved[nreserved + i].size = replaced[i].size;
    }
    info->nreserved = nreserved;
    info->nusable = nusable;
    for (i = 0; i < DT_USABLE; i++) {
      info->usable[i].base = saved[i].base;
      info->usable[i].size = saved[i].size;
    }
    return BOOTSTORE_FULL;
  }
  i = 0;
  if (info->initrd.base & PAGE_MASK) {
    copyedge(selected[i], base, info->initrd.base, end);
    edges[0] = selected[i++];
  }
  if (end & PAGE_MASK) {
    if ((info->initrd.base & PAGE_MASK) && pages == 1)
      edges[1] = edges[0];
    else {
      copyedge(selected[i], ALIGN_DOWN(end, PAGE_SIZE), info->initrd.base, end);
      edges[1] = selected[i++];
    }
  }
  for (i = 0; i < copies; i++) {
    info->bootedge[i].base = selected[i];
    info->bootedge[i].size = PAGE_SIZE;
  }
  store.base = info->initrd.base;
  store.bytes = info->initrd.size;
  store.pages = pages;
  store.copies = copies;
  store.retained = pages + copies;
  store.refs = 0;
  return 0;
}

int
bootstorepage(u64 index, u64 *pa)
{
  if (!store.bytes || !pa || index >= store.pages)
    return BOOTSTORE_BAD;
  if (!index && (store.base & PAGE_MASK))
    *pa = edges[0];
  else if (index == store.pages - 1 && ((store.base + store.bytes) & PAGE_MASK))
    *pa = edges[1];
  else
    *pa = ALIGN_DOWN(store.base, PAGE_SIZE) + index * PAGE_SIZE;
  return 0;
}

int
bootstorestat(Bootstore *out)
{
  if (!out)
    return BOOTSTORE_BAD;
  out->base = store.base;
  out->bytes = store.bytes;
  out->pages = store.pages;
  out->copies = store.copies;
  out->retained = store.retained;
  out->refs = store.refs;
  return 0;
}

int
bootstorehold(void)
{
  if (!store.bytes || store.refs == ~(u64) 0)
    return BOOTSTORE_BAD;
  store.refs++;
  return 0;
}

int
bootstoredrop(void)
{
  if (!store.refs)
    return BOOTSTORE_BAD;
  store.refs--;
  return 0;
}
