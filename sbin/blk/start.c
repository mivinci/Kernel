#include "start.h"
#include "private/syscall.h"
#include "private/virtio/mmio.h"
#include <abi/base.h>
#include <abi/irq.h>
#include <abi/mem.h>
#include <abi/observe.h>
#include <errno.h>

static const u64 tags[BlkHandles] = {BOOT_SELF_SPACE, BOOT_SELF_ALLOC, BLK_BOOT_OBSERVE,
                                     BLK_BOOT_INIT,   BLK_BOOT_MMIO,   BLK_BOOT_IRQ,
                                     BLK_BOOT_DMA};

static isize
decimal(const char *text, u32 bytes, u64 *out)
{
  u64 value = 0;
  u32 i, digit;
  if (!text || !out || bytes < 2 || text[bytes - 1])
    return -ERR_INVALID;
  for (i = 0; i + 1 < bytes; i++) {
    if (text[i] < '0' || text[i] > '9')
      return -ERR_INVALID;
    digit = (u32) (text[i] - '0');
    if (value > (UINT64_MAX - digit) / 10)
      return -ERR_OVERFLOW;
    value = value * 10 + digit;
  }
  *out = value;
  return 0;
}

static int
rightsok(const Blkstart *boot, u32 slot, const Queryrec *row)
{
  const u64 *data = row->data;
  if (row->type != QUERY_RESOURCE || row->pid != boot->pid || row->image != boot->image ||
      row->stamp != (((u64) boot->handle[slot] >> 16) & HANDLE_GENERATION_MAX) ||
      data[0] != (u64) boot->handle[slot] || data[5])
    return 0;
  switch (slot) {
  case BlkSpace:
    return data[1] == RESOURCE_SPACE && data[2] == 3 && !data[3] && !data[4] && !data[6] &&
           !data[7];
  case BlkAlloc:
    return data[1] == RESOURCE_ALLOC && data[2] == 7 && !data[3] && !data[4] && !data[6] &&
           !data[7];
  case BlkObserve:
    return data[1] == RESOURCE_OBSERVE && !(data[2] & ~OBSERVE_RIGHTS) &&
           (data[2] & OBSERVE_SOURCE) && !data[3] && !data[4] && !data[6] && !data[7];
  case BlkInit:
    return data[1] == RESOURCE_CALL && (data[2] & RESOURCE_USE) &&
           !(data[2] & ~(RESOURCE_USE | RESOURCE_GRANT)) && !data[3] && !data[4] && data[6] == 1 &&
           data[7];
  case BlkMmio:
    return data[1] == RESOURCE_MEM && data[2] == (MAP_READ | MAP_WRITE) && !(data[3] & PAGE_MASK) &&
           !(data[4] & PAGE_MASK) && data[3] <= boot->mmiooffset &&
           boot->mmiooffset + boot->mmiobytes >= boot->mmiooffset && data[3] + data[4] >= data[3] &&
           boot->mmiooffset + boot->mmiobytes <= data[3] + data[4] && !data[6] && !data[7];
  case BlkIrq:
    return data[1] == RESOURCE_IRQ_BIND && data[2] == IRQ_BIND_ACK && !data[3] && !data[4] &&
           !data[6] && !data[7];
  case BlkDma:
    return data[1] == RESOURCE_MEM && data[2] == (MAP_READ | MAP_WRITE) && !(data[3] & PAGE_MASK) &&
           !(data[4] & PAGE_MASK) && data[4] >= BLK_DMA_PAGES * PAGE_SIZE && !data[6] && !data[7];
  default:
    return 0;
  }
}

isize
blkstartboot(const Bootview *view, Blkstart *out)
{
  Blkstart        candidate = {0};
  Handle          handles[BlkHandles] = {0};
  const Resentry *entry;
  u64             i, j, k, seen = 0, span;
  isize           error;
  if (!view || !view->header || !view->resources || !view->argv || !out ||
      view->header->argc != 5 + BLK_DMA_PAGES || view->argv[0].length < 2)
    return -ERR_INVALID;
  for (i = 0; i < view->header->resource_count; i++) {
    entry = &view->resources[i];
    for (j = 0; j < BlkHandles; j++) {
      if (entry->tag != tags[j])
        continue;
      if (entry->flags || entry->reserved || seen & (UINT64_C(1) << j) ||
          handlekind(entry->handle) != HANDLE_RESOURCE)
        return -ERR_INVALID;
      for (k = 0; k < BlkHandles; k++)
        if (handles[k] == entry->handle)
          return -ERR_INVALID;
      handles[j] = entry->handle;
      seen |= UINT64_C(1) << j;
      break;
    }
  }
  if (seen != (UINT64_C(1) << BlkHandles) - 1)
    return -ERR_INVALID;
  error = decimal(bootarg(view, 1, 0), view->argv[1].length, &candidate.number);
  if (!error)
    error = decimal(bootarg(view, 2, 0), view->argv[2].length, &candidate.device);
  if (!error)
    error = decimal(bootarg(view, 3, 0), view->argv[3].length, &candidate.mmiooffset);
  if (!error)
    error = decimal(bootarg(view, 4, 0), view->argv[4].length, &candidate.mmiobytes);
  for (j = 0; !error && j < BLK_DMA_PAGES; j++)
    error = decimal(bootarg(view, 5 + j, 0), view->argv[5 + j].length, &candidate.addresses[j]);
  if (error)
    return error;
  if (!candidate.number || candidate.mmiooffset > UINT64_MAX - candidate.mmiobytes ||
      candidate.mmiooffset >= PAGE_SIZE || candidate.mmiobytes < 0x100 ||
      candidate.mmiobytes > PAGE_SIZE - candidate.mmiooffset)
    return -ERR_INVALID;
  span = candidate.mmiooffset + candidate.mmiobytes;
  if (span > PAGE_SIZE || BLK_DMA_BASE < BLK_MMIO_BASE + PAGE_SIZE ||
      BLK_CONN_BASE < BLK_DMA_BASE + BLK_DMA_PAGES * PAGE_SIZE ||
      (u64) FS_CONN_BYTES > UINT64_MAX - BLK_CONN_BASE)
    return -ERR_OVERFLOW;
  for (i = 0; i < BLK_DMA_PAGES; i++) {
    if (!candidate.addresses[i] || candidate.addresses[i] & PAGE_MASK)
      return -ERR_INVALID;
    for (j = 0; j < i; j++)
      if (candidate.addresses[j] == candidate.addresses[i])
        return -ERR_INVALID;
  }
  for (i = 0; i < BlkHandles; i++)
    candidate.handle[i] = handles[i];
  candidate.irqevent = (u64) 1 << IRQ_BIT_MIN;
  *out = candidate;
  return 0;
}

static isize
scan(Blkstart *boot)
{
  Queryrec rows[4];
  u64      cursor = 0, next, written, i, seen = 0;
  u32      batch, slot;
  isize    error;
  for (batch = 0; batch < BOOT_RESOURCE_COUNT; batch++) {
    error = __sys_query(0, QUERY_RESOURCE, cursor, rows, ARRAY_SIZE(rows), &written, &next);
    if (error)
      return error;
    if (written > ARRAY_SIZE(rows) || (next && (!written || next == cursor)))
      return -ERR_STATE;
    for (i = 0; i < written; i++) {
      for (slot = 0; slot < BlkHandles; slot++) {
        if (rows[i].data[0] != (u64) boot->handle[slot])
          continue;
        if (seen & (UINT64_C(1) << slot) || !rightsok(boot, slot, &rows[i]))
          return -ERR_DENIED;
        seen |= UINT64_C(1) << slot;
        if (slot == BlkInit)
          boot->initimage = rows[i].data[7];
      }
    }
    if (seen == (UINT64_C(1) << BlkHandles) - 1)
      return 0;
    if (!next)
      return -ERR_DENIED;
    cursor = next;
  }
  return -ERR_LIMIT;
}

isize
blkstartcheck(Blkstart *boot)
{
  Handle pid = 0, parent = 0;
  u64    image = 0;
  isize  error;
  if (!boot)
    return -ERR_INVALID;
  boot->pid = boot->image = boot->initimage = 0;
  error = __sys_getid(&pid, &image, &parent);
  if (error)
    return error;
  if (pid == 1 || parent != 1 || !image || !pidvalid(pid))
    return -ERR_DENIED;
  boot->pid = pid;
  boot->image = image;
  error = scan(boot);
  if (error) {
    boot->pid = boot->image = boot->initimage = 0;
    return error;
  }
  return 0;
}
