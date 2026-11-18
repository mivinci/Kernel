#include "start.h"
#include <abi/irq.h>
#include <abi/resource.h>
#include <abi/system.h>
#include "private/syscall.h"

static const u64 tags[SerialHandles] = {BOOT_SELF_SPACE, BOOT_SELF_ALLOC, SERIAL_INIT_CALL,
                                        SERIAL_MMIO,     SERIAL_IRQ_BIND, SERIAL_RX_RING,
                                        SERIAL_TX_RING};
static isize
decimal(const char *text, u32 bytes, u64 *out)
{
  u64 value = 0;
  u32 i, digit;
  if (!text || bytes < 2 || text[bytes - 1])
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
equal(const char *text, u32 bytes, const char *expected)
{
  u32 i;
  if (!text || !bytes)
    return 0;
  for (i = 0; i < bytes; i++) {
    if (text[i] != expected[i])
      return 0;
    if (!expected[i])
      return i + 1 == bytes;
  }
  return 0;
}
isize
serialboot(const Bootview *view, Serialboot *out)
{
  Handle          handles[SerialHandles];
  u64             i, j, k, seen = 0, kind, offset, bytes, number, span, shift, width;
  const Resentry *entry;
  const char     *name;
  isize           error;
  if (!view || !view->header || !view->argv || !view->resources || !out ||
      view->header->argc != 7 || view->argv[0].length < 2)
    return -ERR_INVALID;
  name = bootarg(view, 0, 0);
  if (!name || !name[0])
    return -ERR_INVALID;
  for (i = 0; i < SerialHandles; i++)
    handles[i] = 0;
  for (i = 0; i < view->header->resource_count; i++) {
    entry = &view->resources[i];
    for (j = 0; j < SerialHandles; j++) {
      if (entry->tag != tags[j])
        continue;
      if (entry->flags || entry->reserved || seen & (UINT64_C(1) << j) ||
          handlekind(entry->handle) != HANDLE_RESOURCE)
        return -ERR_INVALID;
      for (k = 0; k < SerialHandles; k++)
        if (handles[k] == entry->handle)
          return -ERR_INVALID;
      handles[j] = entry->handle;
      seen |= UINT64_C(1) << j;
      break;
    }
  }
  if (seen != (UINT64_C(1) << SerialHandles) - 1)
    return -ERR_INVALID;
  name = bootarg(view, 1, 0);
  if (equal(name, view->argv[1].length, "ns16550"))
    kind = BOOT_UART_NS16550;
  else if (equal(name, view->argv[1].length, "pl011"))
    kind = BOOT_UART_PL011;
  else
    return -ERR_NOTSUP;
  error = decimal(bootarg(view, 2, 0), view->argv[2].length, &offset);
  if (!error)
    error = decimal(bootarg(view, 3, 0), view->argv[3].length, &bytes);
  if (!error)
    error = decimal(bootarg(view, 4, 0), view->argv[4].length, &number);
  if (!error)
    error = decimal(bootarg(view, 5, 0), view->argv[5].length, &shift);
  if (!error)
    error = decimal(bootarg(view, 6, 0), view->argv[6].length, &width);
  if (error)
    return error;
  if (shift > 4 || (width != 1 && width != 4) || offset >= PAGE_SIZE || (offset & (width - 1)) ||
      (kind == BOOT_UART_NS16550 && ((UINT64_C(1) << shift) & (width - 1))) ||
      (kind == BOOT_UART_PL011 && (shift || width != 4)))
    return -ERR_INVALID;
  if (bytes < (kind == BOOT_UART_PL011 ? 0x4c : (UINT64_C(5) << shift) + width))
    return -ERR_INVALID;
  if (bytes > UINT64_MAX - offset || (span = offset + bytes) > UINT64_MAX - PAGE_MASK)
    return -ERR_OVERFLOW;
  for (i = 0; i < SerialHandles; i++)
    out->handle[i] = handles[i];
  out->kind = kind;
  out->offset = offset;
  out->bytes = bytes;
  out->mapbytes = ALIGN_UP(span, PAGE_SIZE);
  out->number = number;
  out->shift = shift;
  out->width = width;
  out->pid = 0;
  out->image = out->initimage = 0;
  return 0;
}

static int
record(const Serialboot *boot, u32 slot, const Queryrec *row, u64 *initimage)
{
  u64 i, kind, rights, length, offset, target, image;
  kind = row->data[1];
  rights = row->data[2];
  offset = row->data[3];
  length = row->data[4];
  target = row->data[6];
  image = row->data[7];
  if (row->stamp != (((u64) boot->handle[slot] >> 16) & HANDLE_GENERATION_MAX))
    return 0;
  for (i = 8; i < ARRAY_SIZE(row->data); i++)
    if (row->data[i])
      return 0;
  if (slot == SerialMmio || slot == SerialRx || slot == SerialTx)
    return kind == RESOURCE_MEM && rights == (MAP_READ | MAP_WRITE) && !(offset & PAGE_MASK) &&
           !(length & PAGE_MASK) && !row->data[5] && !target && !image &&
           (slot == SerialMmio ? length >= boot->mapbytes : length == SERIAL_RING_BYTES);
  if (offset || length)
    return 0;
  if (slot == SerialInit) {
    if (kind != RESOURCE_CALL || rights != RESOURCE_USE || row->data[5] || target != 1 || !image)
      return 0;
    *initimage = image;
    return 1;
  }
  if (target || image)
    return 0;
  if (slot == SerialSpace)
    return kind == RESOURCE_SPACE && rights == 3 && !row->data[5];
  if (slot == SerialAlloc)
    return kind == RESOURCE_ALLOC && rights == 7 && !row->data[5];
  return kind == RESOURCE_IRQ_BIND && rights == IRQ_BIND_ACK && !row->data[5];
}
isize
serialcheck(Serialboot *boot)
{
  Queryrec rows[4];
  Handle   pid = 0, parent = 0;
  u64      image = 0, initimage = 0, cursor = 0, next, written, seen = 0, i;
  u32      batch, slot;
  isize    error;
  if (!boot)
    return -ERR_INVALID;
  error = __sys_getid(&pid, &image, &parent);
  if (error)
    return error;
  if (pid == 1 || handlekind(pid) != HANDLE_PROCESS || !image || parent != 1)
    return -ERR_DENIED;
  for (batch = 0; batch < BOOT_RESOURCE_COUNT; batch++) {
    written = next = 0;
    error = __sys_query(0, QUERY_RESOURCE, cursor, rows, ARRAY_SIZE(rows), &written, &next);
    if (error)
      return error;
    if (written > ARRAY_SIZE(rows) || (next && (!written || next == cursor)))
      return -ERR_STATE;
    for (i = 0; i < written; i++) {
      if (rows[i].type != QUERY_RESOURCE || rows[i].pid != pid || rows[i].image != image)
        return -ERR_STATE;
      for (slot = 0; slot < SerialHandles; slot++) {
        if (rows[i].data[0] != (u64) boot->handle[slot])
          continue;
        if (seen & (UINT64_C(1) << slot) || !record(boot, slot, &rows[i], &initimage))
          return -ERR_DENIED;
        seen |= UINT64_C(1) << slot;
        break;
      }
    }
    if (seen == (UINT64_C(1) << SerialHandles) - 1) {
      boot->pid = pid;
      boot->image = image;
      boot->initimage = initimage;
      return 0;
    }
    if (!next)
      return -ERR_DENIED;
    cursor = next;
  }
  return -ERR_LIMIT;
}
