#include "private/fileref.h"
static int
identity(Connpeer self)
{
  return pidvalid(self.pid) && self.image;
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
  if (entry->reserved || entry->generation > SYSFILE_GEN_MAX || entry->state > SYSFILE_REF_RESERVED)
    return 0;
  if (entry->state == SYSFILE_REF_RESERVED)
    return entry->generation && !entry->connection.generation && !entry->connection.index &&
           !entry->connection.reserved && !entry->session && !entry->reference && !entry->token &&
           !entry->aliases && !entry->pins && !entry->close_sequence;
  if (entry->state == SYSFILE_REF_FREE || entry->state == SYSFILE_REF_RETIRED)
    return !entry->connection.generation && !entry->connection.index &&
           !entry->connection.reserved && !entry->session && !entry->reference && !entry->token &&
           !entry->aliases && !entry->pins && !entry->close_sequence &&
           (entry->state == SYSFILE_REF_RETIRED ? entry->generation == SYSFILE_GEN_MAX
                                                : entry->generation < SYSFILE_GEN_MAX);
  if (!entry->generation || !entry->connection.generation ||
      entry->connection.index >= SYSCONN_COUNT || entry->connection.reserved || !entry->session ||
      !entry->reference)
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
static isize
forkcheck(Filepool *pool, Connpeer parent, Connpeer child, const Filefork *entries, u64 count,
          u16 selected[SYSFILE_REFS])
{
  u32   i, j;
  isize error;
  if (!pool || !identity(parent) || !identity(child) || parent.pid == child.pid ||
      count > SYSFILE_REFS || (count && !output(pool, entries, count * sizeof(*entries))))
    return -ERR_INVALID;
  error = sysfilerefcheck(pool, parent);
  if (error)
    return error;
  for (i = 0; i < SYSFILE_REFS; i++)
    selected[i] = SYSFILE_REFS;
  for (i = 0; i < count; i++) {
    const Filefork  *plan = &entries[i];
    const Filevalue *value = &plan->value;
    Fileref         *entry;
    error = find(pool, parent, plan->file, &entry);
    if (error)
      return error;
    if (selected[plan->file & 255] != SYSFILE_REFS || (!plan->aliases && !plan->pins) ||
        plan->aliases > entry->aliases || plan->pins > entry->pins || !value->session ||
        value->session != entry->session || !value->reference || !value->token ||
        value->connection.reserved || value->connection.index != entry->connection.index ||
        entry->connection.generation == UINT64_MAX ||
        value->connection.generation != entry->connection.generation + 1)
      return -ERR_INVALID;
    for (j = 0; j < i; j++) {
      const Filefork *other = &entries[j];
      if (value->connection.index == other->value.connection.index &&
          (value->connection.generation != other->value.connection.generation ||
           value->token != other->value.token || value->reference == other->value.reference))
        return -ERR_INVALID;
    }
    selected[plan->file & 255] = (u16) i;
  }
  return 0;
}
isize
sysfilerefforkcheck(Filepool *pool, Connpeer parent, Connpeer child, const Filefork *entries,
                    u64 count)
{
  u16 selected[SYSFILE_REFS];
  return forkcheck(pool, parent, child, entries, count, selected);
}
isize
sysfilereffork(Filepool *pool, Connpeer parent, Connpeer child, const Filefork *entries, u64 count)
{
  u16   selected[SYSFILE_REFS];
  u32   i;
  isize error;
  if (!pool || !identity(parent) || !identity(child) || parent.pid == child.pid ||
      count > SYSFILE_REFS || (count && !output(pool, entries, count * sizeof(*entries))))
    return -ERR_INVALID;
  if (!source(pool, child))
    return 0;
  error = forkcheck(pool, parent, child, entries, count, selected);
  if (error)
    return error;
  for (i = 0; i < SYSFILE_REFS; i++) {
    Fileref        *entry = &pool->entries[i];
    const Filefork *plan = selected[i] == SYSFILE_REFS ? NULL : &entries[selected[i]];
    u64             generation = entry->generation;
    clear(entry);
    entry->generation = generation;
    if (!plan) {
      entry->state = generation == SYSFILE_GEN_MAX ? SYSFILE_REF_RETIRED : SYSFILE_REF_FREE;
      continue;
    }
    entry->connection = plan->value.connection;
    entry->session = plan->value.session;
    entry->reference = plan->value.reference;
    entry->token = plan->value.token;
    entry->aliases = plan->aliases;
    entry->pins = plan->pins;
    entry->state = plan->aliases ? SYSFILE_REF_LIVE : SYSFILE_REF_CLOSING;
  }
  pool->self = child;
  return 0;
}
isize
sysfilerefreserve(Filepool *pool, Connpeer self, u32 count, Filestage *stage)
{
  u32   slots[2], found = 0, i;
  isize error;
  if (!output(pool, stage, sizeof(*stage)) || !count || count > 2 || stage->count ||
      stage->reserved || stage->files[0] || stage->files[1])
    return -ERR_INVALID;
  error = sysfilerefcheck(pool, self);
  if (error)
    return error;
  for (i = 0; i < SYSFILE_REFS && found < count; i++)
    if (pool->entries[i].state == SYSFILE_REF_FREE)
      slots[found++] = i;
  if (found != count)
    return -ERR_CAPACITY;
  for (i = 0; i < count; i++) {
    Fileref *entry = &pool->entries[slots[i]];
    entry->generation++;
    entry->state = SYSFILE_REF_RESERVED;
    stage->files[i] = (entry->generation << 8) | slots[i];
  }
  stage->count = count;
  return 0;
}
static isize
stagecheck(Filepool *pool, Connpeer self, const Filestage *stage, Fileref *entries[2])
{
  u32   i;
  isize error;
  if (!output(pool, stage, sizeof(*stage)) || !stage->count || stage->count > 2 ||
      stage->reserved || (stage->count == 1 && stage->files[1]) ||
      (stage->count == 2 && stage->files[0] == stage->files[1]))
    return -ERR_INVALID;
  error = sysfilerefcheck(pool, self);
  if (error)
    return error;
  for (i = 0; i < stage->count; i++) {
    u64      file = stage->files[i];
    Fileref *entry = &pool->entries[file & 255];
    if (!(file >> 8) || entry->generation != file >> 8 || entry->state != SYSFILE_REF_RESERVED)
      return -ERR_STALE;
    entries[i] = entry;
  }
  return 0;
}
isize
sysfilerefstaged(Filepool *pool, Connpeer self, const Filestage *stage)
{
  Fileref *entries[2];
  return stagecheck(pool, self, stage, entries);
}
isize
sysfilerefcommit(Filepool *pool, Connpeer self, Filestage *owned_stage, const Filevalue *values,
                 u32 closing)
{
  Fileref *entries[2];
  u32      i;
  u64      address = (u64) (uintptr_t) values, bytes;
  isize    error = stagecheck(pool, self, owned_stage, entries);
  if (error)
    return error;
  bytes = owned_stage->count * sizeof(*values);
  if (closing > 1 || !output(pool, values, bytes) || bytes > UINT64_MAX - address ||
      !((u64) (uintptr_t) owned_stage + sizeof(*owned_stage) <= address ||
        address + bytes <= (u64) (uintptr_t) owned_stage))
    return -ERR_INVALID;
  for (i = 0; i < owned_stage->count; i++) {
    const Filevalue *value = &values[i];
    if (!value->connection.generation || value->connection.index >= SYSCONN_COUNT ||
        value->connection.reserved || !value->session || !value->reference)
      return -ERR_INVALID;
  }
  if (owned_stage->count == 2 &&
      values[0].connection.generation == values[1].connection.generation &&
      values[0].connection.index == values[1].connection.index &&
      values[0].session == values[1].session && values[0].reference == values[1].reference)
    return -ERR_INVALID;
  for (i = 0; i < owned_stage->count; i++) {
    Fileref *entry = entries[i];
    entry->connection.generation = values[i].connection.generation;
    entry->connection.index = values[i].connection.index;
    entry->connection.reserved = 0;
    entry->session = values[i].session;
    entry->reference = values[i].reference;
    entry->token = values[i].token;
    entry->aliases = closing ? 0 : 1;
    entry->state = closing ? SYSFILE_REF_CLOSING : SYSFILE_REF_LIVE;
  }
  for (i = 0; i < sizeof(*owned_stage); i++)
    ((u8 *) owned_stage)[i] = 0;
  return 0;
}
isize
sysfilerefabort(Filepool *pool, Connpeer self, Filestage *owned_stage)
{
  Fileref *entries[2];
  u32      i;
  isize    error = stagecheck(pool, self, owned_stage, entries);
  if (error)
    return error;
  for (i = 0; i < owned_stage->count; i++) {
    u64 generation = entries[i]->generation;
    clear(entries[i]);
    entries[i]->generation = generation;
    if (generation == SYSFILE_GEN_MAX)
      entries[i]->state = SYSFILE_REF_RETIRED;
  }
  for (i = 0; i < sizeof(*owned_stage); i++)
    ((u8 *) owned_stage)[i] = 0;
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
sysfilerefnew(Filepool *pool, Connpeer self, Conntag connection, u64 session, u64 reference,
              u64 token, u64 *file)
{
  Fileref *entry;
  u32      i;
  isize    error;
  if (!output(pool, file, sizeof(*file)) || !connection.generation ||
      connection.index >= SYSCONN_COUNT || connection.reserved || !session || !reference)
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
    entry->reference = reference;
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
sysfilerefnextclose(const Filepool *pool, Connpeer self, Conntag connection, u32 minimum, u64 *out)
{
  const Fileref *entry;
  u32            i;
  isize          error = source(pool, self);
  if (error)
    return error;
  if (!output(pool, out, sizeof(*out)) || !connection.generation || connection.reserved ||
      connection.index >= SYSCONN_COUNT || minimum > SYSFILE_REFS)
    return -ERR_INVALID;
  for (i = minimum; i < SYSFILE_REFS; i++) {
    entry = &pool->entries[i];
    if (!valid(entry))
      return -ERR_STATE;
    if (entry->state != SYSFILE_REF_CLOSING || entry->pins || entry->close_sequence ||
        entry->connection.index != connection.index ||
        entry->connection.generation != connection.generation)
      continue;
    *out = (entry->generation << 8) | i;
    return 0;
  }
  return -ERR_WOULD_BLOCK;
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
sysfilerefunclaimclose(Filepool *pool, Connpeer self, const Fileclose *key)
{
  Fileref *entry;
  isize    error;
  if (!output(pool, key, sizeof(*key)))
    return -ERR_INVALID;
  error = find(pool, self, key->file, &entry);
  if (error)
    return error;
  if (!key->sequence || entry->state != SYSFILE_REF_CLOSING ||
      entry->close_sequence != key->sequence)
    return -ERR_STALE;
  entry->close_sequence = 0;
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
