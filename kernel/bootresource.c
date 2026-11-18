#include <bootresource.h>
#include <device.h>
static Imageid  owner;
static Queryrec record;
static u32      prepared, available, irqnumber;

static void
copyrecord(Queryrec *out, const Queryrec *in)
{
  u32 i;
  for (i = 0; i < sizeof(*out); i++)
    ((volatile u8 *) out)[i] = ((const u8 *) in)[i];
}

static int
grants(Rtable *table, const Queryrec *row, u32 number)
{
  const Local *slot;
  Memid        id;
  Mem          memory;
  Device       device;
  u32          actual, expected;
  if (localnative(table, (Handle) row->data[0]))
    return -ERR_STALE;
  slot = &table->slots[handleindex((Handle) row->data[0])];
  id.generation = slot->objectgen;
  id.index = slot->object;
  id.reserved = 0;
  if (slot->subtype != LOCAL_MEM || slot->rights != (VM_READ | VM_WRITE | LOCAL_GRANT) ||
      slot->inherit || slot->offset || memstat(id, &memory) || memory.kind != MEM_MMIO ||
      !memory.managers || slot->length != memory.bytes || devstat(memory.head, &device) ||
      !device.allowed || device.kind != row->data[1] || device.actual.base != row->data[2] ||
      device.actual.size != row->data[3])
    return -ERR_STALE;
  if (row->data[4]) {
    if (!number || localirqresolve(table, (Handle) row->data[4], &actual) ||
        irqsource(number, &expected) || actual != expected)
      return -ERR_STALE;
  } else if (number)
    return -ERR_INVALID;
  return 0;
}
int
bootresourceinit(Rtable *table, Imageid image, const Dtuart *uart, Handle memory, Handle irq,
                 u32 number)
{
  Image    state;
  Queryrec candidate;
  u32      i;
  int      error;
  if (prepared)
    return -ERR_STATE;
  if (!table || table->state != LOCAL_OPEN || table->owner != 1 || table->image != 1 ||
      imagestat(image, &state) || state.pid != table->owner || state.number != table->image ||
      state.state != IMAGE_PREPARING)
    return -ERR_DENIED;
  for (i = 0; i < sizeof(candidate); i++)
    ((u8 *) &candidate)[i] = 0;
  if (memory) {
    if (!uart || uart->reserved || uart->shift > 4 || (uart->width != 1 && uart->width != 4) ||
        (uart->reg.base & (uart->width - 1)) ||
        (uart->kind == BOOT_UART_NS16550 ? ((1U << uart->shift) & (uart->width - 1))
         : uart->kind == BOOT_UART_PL011 ? (uart->shift || uart->width != 4)
                                         : 1))
      return -ERR_NOTSUP;
    candidate.type = QUERY_BOOT_RESOURCE;
    candidate.pid = state.pid;
    candidate.image = state.number;
    candidate.stamp = 1;
    candidate.data[0] = (u64) memory;
    candidate.data[1] = uart->kind;
    candidate.data[2] = uart->reg.base;
    candidate.data[3] = uart->reg.size;
    candidate.data[4] = (u64) irq;
    candidate.data[6] = uart->shift;
    candidate.data[7] = uart->width;
    error = grants(table, &candidate, number);
    if (error)
      return error;
  } else if (uart || irq || number)
    return -ERR_INVALID;
  owner = image;
  copyrecord(&record, &candidate);
  irqnumber = number;
  available = memory != 0;
  prepared = 1;
  return 0;
}
int
bootresourcequery(Rtable *table, Handle system, u64 cursor, Queryrec *records, u32 count,
                  u32 *written, u64 *next)
{
  Imageid image;
  int     error;
  if (!written || !next)
    return -ERR_INVALID;
  *written = 0;
  *next = 0;
  if (!records || !count || count > QUERY_RECORDS)
    return -ERR_INVALID;
  if (!system)
    return -ERR_DENIED;
  error = localsystemquery(table, system, &image);
  if (error)
    return error;
  if (!prepared || image.generation != owner.generation || image.index != owner.index)
    return -ERR_DENIED;
  if (cursor)
    return -ERR_STALE; /* One selected UART, hence no continuation cursor. */
  if (available) {
    error = grants(table, &record, irqnumber);
    if (error)
      return error;
    copyrecord(&records[0], &record);
    *written = 1;
  }
  return 0;
}
