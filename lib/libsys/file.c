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
isize
sysfileargs(u64 opcode, const Fileslot *slot)
{
  u32 used, i;
  if (opcode == FS_READ || opcode == FS_WRITE)
    used = 4;
  else if (opcode == FS_CLOSE || opcode == FS_DUPREF || opcode == FS_SYNC)
    used = 1;
  else if (opcode == FS_SEEK)
    used = 2;
  else
    return -ERR_NOTSUP;
  if (!slot || slot->span != 1 || slot->payload_bytes > FS_SLOT_BYTES || !slot->object)
    return -ERR_INVALID;
  for (i = used; i < ARRAY_SIZE(slot->args); i++)
    if (slot->args[i])
      return -ERR_INVALID;
  if (opcode == FS_SYNC)
    return !slot->payload_bytes && slot->args[0] <= 1 ? 0 : -ERR_INVALID;
  if (used == 1)
    return slot->args[0] && !slot->payload_bytes ? 0 : -ERR_INVALID;
  if (opcode == FS_SEEK)
    return !slot->payload_bytes && slot->args[1] <= FS_SEEK_END ? 0 : -ERR_INVALID;
  if (slot->args[2] > slot->payload_bytes || slot->args[1] > slot->payload_bytes - slot->args[2] ||
      (slot->args[3] & ~(u64) (FS_IO_POSITIONAL | (opcode == FS_WRITE ? FS_IO_APPEND : 0))))
    return -ERR_INVALID;
  return 0;
}
isize
sysfilepollrows(const Fileinflight *active, Handle request, const volatile void *region, u64 bytes,
                u32 index, const Fileslot *slot, Filepoll *out, u32 capacity)
{
  const volatile u8 *base = region;
  u64                length;
  u32                i;
  if (!active || !slot || bytes != FS_CONN_BYTES || index >= FS_CONN_SLOTS ||
      handlekind(request) != HANDLE_REQUEST || slot->span != 1 || slot->object || !slot->args[0] ||
      slot->args[0] > FS_POLL_ROWS || slot->args[0] > capacity)
    return -ERR_INVALID;
  if (active->requests[index] != request)
    return -ERR_STALE;
  for (i = 3; i < ARRAY_SIZE(slot->args); i++)
    if (slot->args[i])
      return -ERR_INVALID;
  length = slot->args[0] * sizeof(*out);
  if (slot->payload_bytes != length || !separate(region, bytes, out, length) ||
      !separate(active, sizeof(*active), out, length) ||
      !separate(slot, sizeof(*slot), out, length))
    return -ERR_INVALID;
  snapshot(out, base + FS_DATA_OFFSET + index * FS_SLOT_BYTES, length);
  return 0;
}

static int
pollpage(const volatile void *region, u64 bytes, Filelease lease, const Filepoll *rows, u32 count)
{
  return bytes == FS_CONN_BYTES && !((uintptr_t) region & 7) && lease.seq && lease.span == 1 &&
         spanvalid(lease.slot, lease.span) && count && count <= FS_POLL_ROWS &&
         separate(region, bytes, rows, count * sizeof(*rows));
}

isize
sysfilepollstage(volatile void *region, u64 bytes, Filelease lease, const Filepoll *rows, u32 count,
                 u64 deadline, u64 generation)
{
  volatile u8       *base = region;
  volatile Filepoll *output;
  volatile u8       *descriptor;
  Fileslot           slot;
  u32                i;
  if (!generation || !pollpage(region, bytes, lease, rows, count))
    return -ERR_INVALID;
  for (i = 0; i < count; i++)
    if (!rows[i].session || !rows[i].fd_generation ||
        rows[i].events & ~(u64) (FS_POLL_IN | FS_POLL_PRI | FS_POLL_OUT))
      return -ERR_INVALID;
  output = (volatile Filepoll *) (base + FS_DATA_OFFSET + lease.slot * FS_SLOT_BYTES);
  for (i = 0; i < count; i++) {
    output[i].session = rows[i].session;
    output[i].fd_generation = rows[i].fd_generation;
    output[i].events = rows[i].events;
    output[i].revents = 0;
  }
  for (i = 0; i < sizeof(slot); i++)
    ((volatile u8 *) &slot)[i] = 0;
  slot.seq = lease.seq;
  slot.state = FS_SLOT_SUBMITTED;
  slot.span = lease.span;
  slot.payload_bytes = count * sizeof(*rows);
  slot.args[0] = count;
  slot.args[1] = deadline;
  slot.args[2] = generation;
  descriptor = base + FS_DESC_OFFSET + lease.slot * FS_DESC_BYTES;
  for (i = 0; i < sizeof(slot); i++)
    descriptor[i] = ((const u8 *) &slot)[i];
  return 0;
}

isize
sysfilepollresult(const volatile void *region, u64 bytes, Filelease lease, const u64 words[4],
                  Filepoll *rows, u32 count)
{
  const volatile u8       *base = region;
  const volatile Filepoll *page;
  u64                      events[FS_POLL_ROWS];
  u32                      i, ready = 0;
  if (!words || !pollpage(region, bytes, lease, rows, count))
    return -ERR_INVALID;
  if (words[0] || words[1] != lease.seq || words[2] > count || words[3] != count * sizeof(*rows))
    return -ERR_STATE;
  page = (const volatile Filepoll *) (base + FS_DATA_OFFSET + lease.slot * FS_SLOT_BYTES);
  for (i = 0; i < count; i++) {
    u64 ordinary = rows[i].events & (FS_POLL_IN | FS_POLL_PRI | FS_POLL_OUT);
    events[i] = page[i].revents;
    if (page[i].session != rows[i].session || page[i].fd_generation != rows[i].fd_generation ||
        page[i].events != rows[i].events || events[i] & ~(ordinary | FS_POLL_ERR | FS_POLL_HUP))
      return -ERR_STATE;
    ready += !!events[i];
  }
  if (ready != words[2])
    return -ERR_STATE;
  for (i = 0; i < count; i++)
    rows[i].revents = events[i];
  return ready;
}

isize
sysfileaccept(Fileinflight *active, const volatile void *region, u64 bytes, u64 slot, u64 seq,
              Handle request, Fileslot *out)
{
  Fileslot descriptor;
  isize    status;
  u32      i;
  if (slot >= FS_CONN_SLOTS || handlekind(request) != HANDLE_REQUEST ||
      !separate(region, bytes, active, sizeof(*active)) ||
      !separate(region, bytes, out, sizeof(*out)) ||
      !separate(active, sizeof(*active), out, sizeof(*out)))
    return -ERR_INVALID;
  if (active->requests[slot])
    return -ERR_BUSY;
  for (i = 0; i < FS_CONN_SLOTS; i++)
    if (active->requests[i] == request)
      return -ERR_BUSY;
  status = sysfilerequest(region, bytes, (u32) slot, seq, &descriptor);
  if (status)
    return status;
  for (i = 0; i < descriptor.span; i++)
    if (active->requests[slot + i])
      return -ERR_BUSY;
  for (i = 0; i < descriptor.span; i++)
    active->requests[slot + i] = request;
  snapshot(out, &descriptor, sizeof(descriptor));
  return 0;
}
isize
sysfilefinish(Fileinflight *active, Handle request)
{
  u32 i;
  int found = 0;
  if (!active || handlekind(request) != HANDLE_REQUEST)
    return -ERR_INVALID;
  for (i = 0; i < FS_CONN_SLOTS; i++)
    if (active->requests[i] == request) {
      active->requests[i] = 0;
      found = 1;
    }
  return found ? 0 : -ERR_STALE;
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
