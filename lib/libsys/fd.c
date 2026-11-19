#include "private/fd.h"
static int
valid(const Fdentry *entry)
{
  if (entry->reserved || entry->state > SYSFD_RETIRED || entry->flags & ~((u64) BOOT_FD_CLOEXEC))
    return 0;
  if (entry->state == SYSFD_LIVE)
    return entry->generation && entry->file;
  if (entry->file || entry->flags)
    return 0;
  if (entry->state == SYSFD_RESERVED)
    return entry->generation != 0;
  return entry->state == SYSFD_RETIRED ? entry->generation == UINT64_MAX
                                       : entry->generation != UINT64_MAX;
}
static int
output(const Fdtable *table, const void *out, u64 bytes)
{
  u64 base = (u64) (uintptr_t) table, address = (u64) (uintptr_t) out;
  return table && out && sizeof(*table) <= UINT64_MAX - base && bytes <= UINT64_MAX - address &&
         (address + bytes <= base || base + sizeof(*table) <= address);
}
static void
observe(const Fdentry *entry, u32 fd, Fdview *out)
{
  out->tag.generation = entry->generation;
  out->tag.index = fd;
  out->tag.state = entry->state;
  out->file = entry->file;
  out->flags = entry->flags;
}
static isize
match(Fdtable *table, Fdtag tag, Fdentry **out)
{
  Fdentry *entry;
  if (!table || tag.state > SYSFD_RETIRED)
    return -ERR_INVALID;
  if (tag.index >= BOOT_FD_COUNT)
    return -ERR_BAD_HANDLE;
  entry = &table->entries[tag.index];
  if (!valid(entry))
    return -ERR_STATE;
  if (entry->generation != tag.generation || entry->state != tag.state)
    return -ERR_STALE;
  *out = entry;
  return 0;
}
static void
retire(Fdentry *entry)
{
  entry->file = entry->flags = 0;
  if (entry->generation != UINT64_MAX)
    entry->generation++;
  entry->state = entry->generation == UINT64_MAX ? SYSFD_RETIRED : SYSFD_FREE;
}
void
sysfdinit(Fdtable *table)
{
  u32 i;
  for (i = 0; i < BOOT_FD_COUNT; i++) {
    table->entries[i].generation = 0;
    table->entries[i].file = table->entries[i].flags = 0;
    table->entries[i].state = SYSFD_FREE;
    table->entries[i].reserved = 0;
  }
}
isize
sysfdcheck(const Fdtable *table)
{
  u32 i;
  if (!table)
    return -ERR_INVALID;
  for (i = 0; i < BOOT_FD_COUNT; i++)
    if (!valid(&table->entries[i]))
      return -ERR_STATE;
  return 0;
}
isize
sysfdobserve(const Fdtable *table, u32 fd, Fdview *out)
{
  if (!output(table, out, sizeof(*out)))
    return -ERR_INVALID;
  if (fd >= BOOT_FD_COUNT)
    return -ERR_BAD_HANDLE;
  if (!valid(&table->entries[fd]))
    return -ERR_STATE;
  observe(&table->entries[fd], fd, out);
  return 0;
}
isize
sysfdreserve(Fdtable *table, u32 minimum, Fdtag *out)
{
  u32      i;
  Fdentry *entry;
  if (!output(table, out, sizeof(*out)) || minimum >= BOOT_FD_COUNT)
    return -ERR_INVALID;
  for (i = minimum; i < BOOT_FD_COUNT; i++) {
    entry = &table->entries[i];
    if (!valid(entry))
      return -ERR_STATE;
    if (entry->state != SYSFD_FREE)
      continue;
    entry->generation++;
    entry->state = SYSFD_RESERVED;
    out->generation = entry->generation;
    out->index = i;
    out->state = SYSFD_RESERVED;
    return 0;
  }
  return -ERR_CAPACITY;
}
isize
sysfdpublish(Fdtable *table, Fdtag reserved, u64 file, u64 flags)
{
  Fdentry *entry;
  isize    error;
  if (!file || flags & ~((u64) BOOT_FD_CLOEXEC))
    return -ERR_INVALID;
  error = match(table, reserved, &entry);
  if (error)
    return error;
  if (entry->state != SYSFD_RESERVED)
    return -ERR_STATE;
  entry->file = file;
  entry->flags = flags;
  entry->state = SYSFD_LIVE;
  return 0;
}
isize
sysfdcancel(Fdtable *table, Fdtag reserved)
{
  Fdentry *entry;
  isize    error = match(table, reserved, &entry);
  if (error)
    return error;
  if (entry->state != SYSFD_RESERVED)
    return -ERR_STATE;
  retire(entry);
  return 0;
}
isize
sysfdreplace(Fdtable *table, Fdtag expected, u64 file, u64 flags, Fdview *out)
{
  Fdentry *entry;
  isize    error;
  if (!output(table, out, sizeof(*out)) || !file || flags & ~((u64) BOOT_FD_CLOEXEC))
    return -ERR_INVALID;
  error = match(table, expected, &entry);
  if (error)
    return error;
  if (entry->state == SYSFD_RESERVED)
    return -ERR_BUSY;
  if (entry->state == SYSFD_RETIRED || entry->generation == UINT64_MAX)
    return -ERR_OVERFLOW;
  observe(entry, expected.index, out);
  entry->generation++;
  entry->file = file;
  entry->flags = flags;
  entry->state = SYSFD_LIVE;
  return 0;
}
isize
sysfddetach(Fdtable *table, Fdtag expected, Fdview *out)
{
  Fdentry *entry;
  isize    error;
  if (!output(table, out, sizeof(*out)))
    return -ERR_INVALID;
  error = match(table, expected, &entry);
  if (error)
    return error;
  if (entry->state != SYSFD_LIVE)
    return -ERR_BAD_HANDLE;
  observe(entry, expected.index, out);
  retire(entry);
  return 0;
}
isize
sysfdsetflags(Fdtable *table, Fdtag expected, u64 flags)
{
  Fdentry *entry;
  isize    error;
  if (flags & ~((u64) BOOT_FD_CLOEXEC))
    return -ERR_INVALID;
  error = match(table, expected, &entry);
  if (error)
    return error;
  if (entry->state != SYSFD_LIVE)
    return -ERR_BAD_HANDLE;
  entry->flags = flags;
  return 0;
}
