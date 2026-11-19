#include "private/file.h"
static int
separate(const volatile void *base, u64 bytes, const void *out, u64 size)
{
  u64 address = (u64) (uintptr_t) base, target = (u64) (uintptr_t) out;
  return base && out && bytes <= UINT64_MAX - address && size <= UINT64_MAX - target &&
         (address + bytes <= target || target + size <= address);
}
static void
snapshot(void *out, const volatile void *from, u64 bytes)
{
  u8                *target = out;
  const volatile u8 *source = from;
  u64                i;
  for (i = 0; i < bytes; i++)
    target[i] = source[i];
}
static int
spanvalid(u32 slot, u32 span)
{
  return slot < FS_CONN_SLOTS && (span == 1 || (span == 2 && slot <= 4 && !(slot & 1)));
}
isize
sysfilehead(const volatile void *region, u64 bytes, u64 connid, u64 serverimage, Filehead *out)
{
  Filehead head;
  if (bytes != FS_CONN_BYTES || !(connid >> 16) || (connid & 65535) >= FS_CONN_COUNT ||
      !serverimage || !separate(region, bytes, out, sizeof(*out)))
    return -ERR_INVALID;
  snapshot(&head, region, sizeof(head));
  if (head.magic != FS_CONN_MAGIC || head.bytes != bytes || head.slots != FS_CONN_SLOTS ||
      head.slotbytes != FS_SLOT_BYTES || head.reserved[0] || head.reserved[1])
    return -ERR_INVALID;
  if (head.connid != connid || head.serverimage != serverimage)
    return -ERR_STALE;
  snapshot(out, &head, sizeof(head));
  return 0;
}
isize
sysfilerequest(const volatile void *region, u64 bytes, u32 slot, u64 seq, Fileslot *out)
{
  Fileslot           descriptor, next;
  const volatile u8 *base = region;
  const u8          *raw = (const u8 *) &next;
  u32                i;
  if (bytes != FS_CONN_BYTES || !seq || slot >= FS_CONN_SLOTS ||
      !separate(region, bytes, out, sizeof(*out)))
    return -ERR_INVALID;
  snapshot(&descriptor, base + FS_DESC_OFFSET + slot * FS_DESC_BYTES, sizeof(descriptor));
  if (descriptor.seq != seq)
    return -ERR_STALE;
  if (descriptor.state != FS_SLOT_SUBMITTED || descriptor.span > 2 ||
      !spanvalid(slot, (u32) descriptor.span) ||
      descriptor.payload_bytes > descriptor.span * FS_SLOT_BYTES || descriptor.reserved[0] ||
      descriptor.reserved[1] || descriptor.reserved[2])
    return -ERR_INVALID;
  if (descriptor.span == 2) {
    snapshot(&next, base + FS_DESC_OFFSET + (slot + 1) * FS_DESC_BYTES, sizeof(next));
    for (i = 0; i < sizeof(next); i++)
      if (raw[i])
        return -ERR_INVALID;
  }
  snapshot(out, &descriptor, sizeof(descriptor));
  return 0;
}
void
sysfilepagesinit(Filepages *pages)
{
  u32 i;
  pages->sequence = 0;
  for (i = 0; i < FS_CONN_SLOTS; i++) {
    pages->pages[i].seq = 0;
    pages->pages[i].slot = pages->pages[i].span = 0;
  }
}
isize
sysfilepagescheck(const Filepages *pages)
{
  const Filelease *page, *head;
  u32              i, j;
  if (!pages)
    return -ERR_INVALID;
  for (i = 0; i < FS_CONN_SLOTS; i++) {
    page = &pages->pages[i];
    if (!page->seq) {
      if (page->slot || page->span)
        return -ERR_STATE;
      continue;
    }
    if (page->seq > pages->sequence || !spanvalid(page->slot, page->span) || i < page->slot ||
        i - page->slot >= page->span)
      return -ERR_STATE;
    head = &pages->pages[page->slot];
    if (head->seq != page->seq || head->span != page->span || head->slot != page->slot)
      return -ERR_STATE;
    for (j = 0; j < page->span; j++) {
      const Filelease *part = &pages->pages[page->slot + j];
      if (part->seq != page->seq || part->span != page->span || part->slot != page->slot)
        return -ERR_STATE;
    }
    for (j = 0; j < i; j++)
      if (pages->pages[j].seq == page->seq && pages->pages[j].slot != page->slot)
        return -ERR_STATE;
  }
  return 0;
}
isize
sysfilereserve(Filepages *pages, u32 context, u32 span, Filelease *out)
{
  u32 first, count, slot, i;
  if (context > SYSFILE_CLEANUP || !span || span > 2 || (context >= SYSFILE_CONTROL && span != 1) ||
      !separate(pages, sizeof(*pages), out, sizeof(*out)))
    return -ERR_INVALID;
  if (sysfilepagescheck(pages))
    return -ERR_STATE;
  if (pages->sequence == UINT64_MAX)
    return -ERR_OVERFLOW;
  first = context < SYSFILE_CONTROL ? context * 2 : context + 3;
  count = context < SYSFILE_CONTROL ? 2 : 1;
  for (slot = first; slot + span <= first + count; slot++) {
    if (!spanvalid(slot, span))
      continue;
    for (i = 0; i < span && !pages->pages[slot + i].seq; i++) {
    }
    if (i != span)
      continue;
    pages->sequence++;
    for (i = 0; i < span; i++) {
      pages->pages[slot + i].seq = pages->sequence;
      pages->pages[slot + i].slot = slot;
      pages->pages[slot + i].span = span;
    }
    out->seq = pages->sequence;
    out->slot = slot;
    out->span = span;
    return 0;
  }
  return -ERR_BUSY;
}
isize
sysfilerelease(Filepages *pages, Filelease expected)
{
  u32 i;
  if (!pages || !expected.seq || !spanvalid(expected.slot, expected.span))
    return -ERR_INVALID;
  if (sysfilepagescheck(pages))
    return -ERR_STATE;
  for (i = 0; i < expected.span; i++) {
    const Filelease *page = &pages->pages[expected.slot + i];
    if (page->seq != expected.seq || page->slot != expected.slot || page->span != expected.span)
      return -ERR_STALE;
  }
  for (i = 0; i < expected.span; i++) {
    pages->pages[expected.slot + i].seq = 0;
    pages->pages[expected.slot + i].slot = pages->pages[expected.slot + i].span = 0;
  }
  return 0;
}
