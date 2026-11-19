#include "private/fileref.h"
static int
identity(Connpeer self)
{
  return handlekind(self.pid) == HANDLE_PROCESS && self.image;
}
static isize
source(const Filepool *pool, Connpeer self)
{
  if (!pool || !identity(self))
    return -ERR_INVALID;
  return pool->self.pid == self.pid && pool->self.image == self.image ? 0 : -ERR_STALE;
}
static int
output(const Filepool *pool, const void *out, u64 bytes)
{
  u64 base = (u64) (uintptr_t) pool, address = (u64) (uintptr_t) out;
  return pool && out && sizeof(*pool) <= UINT64_MAX - base && bytes <= UINT64_MAX - address &&
         (base + sizeof(*pool) <= address || address + bytes <= base);
}
static int
valid(const Fileref *entry)
{
  if (entry->reserved || entry->generation > SYSFILE_GEN_MAX || entry->state > SYSFILE_REF_RETIRED)
    return 0;
  if (entry->state == SYSFILE_REF_FREE || entry->state == SYSFILE_REF_RETIRED)
    return !entry->connection.generation && !entry->connection.index &&
           !entry->connection.reserved && !entry->session && !entry->token && !entry->aliases &&
           !entry->pins && !entry->close_sequence &&
           (entry->state == SYSFILE_REF_RETIRED ? entry->generation == SYSFILE_GEN_MAX
                                                : entry->generation < SYSFILE_GEN_MAX);
  if (!entry->generation || !entry->connection.generation ||
      entry->connection.index >= SYSCONN_COUNT || entry->connection.reserved || !entry->session)
    return 0;
  return entry->state == SYSFILE_REF_LIVE
             ? entry->aliases && !entry->close_sequence
             : !entry->aliases && (!entry->close_sequence || !entry->pins);
}
static isize
find(Filepool *pool, Connpeer self, u64 file, Fileref **out)
{
  Fileref *entry;
  isize    error = source(pool, self);
  if (error)
    return error;
  if (!(file >> 8))
    return -ERR_BAD_HANDLE;
  entry = &pool->entries[file & 255];
  if (!valid(entry))
    return -ERR_STATE;
  if (entry->generation != file >> 8 ||
      (entry->state != SYSFILE_REF_LIVE && entry->state != SYSFILE_REF_CLOSING))
    return -ERR_STALE;
  *out = entry;
  return 0;
}
static void
clear(Fileref *entry)
{
  u32 i;
  for (i = 0; i < sizeof(*entry); i++)
    ((u8 *) entry)[i] = 0;
}
isize
sysfilerefinit(Filepool *pool, Connpeer self)
{
  u32 i;
  if (!pool || !identity(self))
    return -ERR_INVALID;
  pool->self.pid = self.pid;
  pool->self.image = self.image;
  pool->sequence = 0;
  for (i = 0; i < SYSFILE_REFS; i++)
    clear(&pool->entries[i]);
  return 0;
}
isize
sysfilerefcheck(const Filepool *pool, Connpeer self)
{
  u32   i;
  isize error = source(pool, self);
  if (error)
    return error;
  for (i = 0; i < SYSFILE_REFS; i++)
    if (!valid(&pool->entries[i]) || pool->entries[i].close_sequence > pool->sequence)
      return -ERR_STATE;
  return 0;
}
isize
sysfilerefconnection(const Filepool *pool, Connpeer self, Conntag connection)
{
  u32   i;
  isize error;
  if (!connection.generation || connection.index >= SYSCONN_COUNT || connection.reserved)
    return -ERR_INVALID;
  error = sysfilerefcheck(pool, self);
  if (error)
    return error;
  for (i = 0; i < SYSFILE_REFS; i++) {
    const Fileref *entry = &pool->entries[i];
    if ((entry->state == SYSFILE_REF_LIVE || entry->state == SYSFILE_REF_CLOSING) &&
        entry->connection.generation == connection.generation &&
        entry->connection.index == connection.index)
      return 1;
  }
  return 0;
}
isize
sysfilerefnew(Filepool *pool, Connpeer self, Conntag connection, u64 session, u64 token, u64 *file)
{
  Fileref *entry;
  u32      i;
  isize    error;
  if (!output(pool, file, sizeof(*file)) || !connection.generation ||
      connection.index >= SYSCONN_COUNT || connection.reserved || !session)
    return -ERR_INVALID;
  error = sysfilerefcheck(pool, self);
  if (error)
    return error;
  for (i = 0; i < SYSFILE_REFS; i++) {
    entry = &pool->entries[i];
    if (entry->state != SYSFILE_REF_FREE)
      continue;
    entry->generation++;
    entry->connection.generation = connection.generation;
    entry->connection.index = connection.index;
    entry->session = session;
    entry->token = token;
    entry->aliases = 1;
    entry->state = SYSFILE_REF_LIVE;
    *file = (entry->generation << 8) | i;
    return 0;
  }
  return -ERR_CAPACITY;
}
isize
sysfilerefview(Filepool *pool, Connpeer self, u64 file, Fileref *out)
{
  Fileref *entry;
  u32      i;
  isize    error;
  if (!output(pool, out, sizeof(*out)))
    return -ERR_INVALID;
  error = find(pool, self, file, &entry);
  if (error)
    return error;
  for (i = 0; i < sizeof(*out); i++)
    ((u8 *) out)[i] = ((const u8 *) entry)[i];
  return 0;
}
isize
sysfilerefalias(Filepool *pool, Connpeer self, u64 file)
{
  Fileref *entry;
  isize    error = find(pool, self, file, &entry);
  if (error)
    return error;
  if (entry->state != SYSFILE_REF_LIVE)
    return -ERR_STATE;
  if (entry->aliases == UINT32_MAX)
    return -ERR_OVERFLOW;
  entry->aliases++;
  return 0;
}
isize
sysfilerefdrop(Filepool *pool, Connpeer self, u64 file)
{
  Fileref *entry;
  isize    error = find(pool, self, file, &entry);
  if (error)
    return error;
  if (entry->state != SYSFILE_REF_LIVE || !entry->aliases)
    return -ERR_STATE;
  if (!--entry->aliases)
    entry->state = SYSFILE_REF_CLOSING;
  return 0;
}
isize
sysfilerefpin(Filepool *pool, Connpeer self, u64 file)
{
  Fileref *entry;
  isize    error = find(pool, self, file, &entry);
  if (error)
    return error;
  if (entry->state != SYSFILE_REF_LIVE)
    return -ERR_STATE;
  if (entry->pins == UINT32_MAX)
    return -ERR_OVERFLOW;
  entry->pins++;
  return 0;
}
isize
sysfilerefunpin(Filepool *pool, Connpeer self, u64 file)
{
  Fileref *entry;
  isize    error = find(pool, self, file, &entry);
  if (error)
    return error;
  if (!entry->pins)
    return -ERR_STATE;
  entry->pins--;
  return 0;
}
isize
sysfilerefclose(Filepool *pool, Connpeer self, u64 file, Fileclose *out)
{
  Fileref *entry;
  isize    error;
  if (!output(pool, out, sizeof(*out)))
    return -ERR_INVALID;
  error = find(pool, self, file, &entry);
  if (error)
    return error;
  if (entry->state != SYSFILE_REF_CLOSING || entry->pins || entry->close_sequence)
    return -ERR_BUSY;
  if (pool->sequence == UINT64_MAX)
    return -ERR_OVERFLOW;
  entry->close_sequence = ++pool->sequence;
  out->file = file;
  out->sequence = entry->close_sequence;
  return 0;
}
isize
sysfilerefconfirmed(Filepool *pool, Connpeer self, const Fileclose *key)
{
  Fileref *entry;
  u64      generation;
  isize    error;
  if (!output(pool, key, sizeof(*key)))
    return -ERR_INVALID;
  error = find(pool, self, key->file, &entry);
  if (error)
    return error;
  if (entry->state != SYSFILE_REF_CLOSING || entry->aliases || entry->pins || !key->sequence ||
      entry->close_sequence != key->sequence)
    return -ERR_STALE;
  generation = entry->generation;
  clear(entry);
  entry->generation = generation;
  if (generation == SYSFILE_GEN_MAX)
    entry->state = SYSFILE_REF_RETIRED;
  return 0;
}
