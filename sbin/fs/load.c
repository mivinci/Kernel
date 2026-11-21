#include "load.h"
#include "cache.h"
#include <proto/file.h>

static Loadslot *
lookup(Loadtable *table, u64 id)
{
  u32 i;
  if (table && id)
    for (i = 0; i < LOAD_SLOTS; i++)
      if (table->slots[i].id == id)
        return &table->slots[i];
  return NULL;
}

static int
samefile(const Loadfile *a, const Loadfile *b)
{
  return a->inode == b->inode && a->generation == b->generation && a->version == b->version &&
         a->length == b->length;
}

static isize
datablock(const Loadmap *map, const Loadindex *index, u32 start, u32 blocks, u32 logical, u32 *out)
{
  u32 block;
  if (logical < LOAD_DIRECT)
    block = map->direct[logical];
  else if (!map->indirect)
    block = 0;
  else {
    if (!index)
      return -ERR_WOULD_BLOCK;
    if (index->block != map->indirect)
      return -ERR_STALE;
    block = index->entries[logical - LOAD_DIRECT];
  }
  if (block && (block < start || block >= blocks || block == map->indirect))
    return -ERR_STATE;
  *out = block;
  return 0;
}

isize
loadmapspan(const Loadfile *file, const Loadmap *map, const Loadindex *index, u32 data_start,
            u32 blocks, u64 offset, u64 bytes, Loadspan *out)
{
  u32   first, next = 0, logical;
  isize error;
  if (!file || !map || !out || !data_start || data_start >= blocks || !bytes ||
      bytes > FS_SLOT_BYTES || file->length > 4 * 1024 * 1024 || offset > file->length ||
      bytes > file->length - offset)
    return -ERR_INVALID;
  if (!samefile(file, &map->file))
    return -ERR_STALE;
  if (map->indirect && (map->indirect < data_start || map->indirect >= blocks))
    return -ERR_STATE;
  logical = (u32) (offset / CACHE_BYTES);
  error = datablock(map, index, data_start, blocks, logical, &first);
  if (!error && (offset + bytes - 1) / CACHE_BYTES != logical)
    error = datablock(map, index, data_start, blocks, logical + 1, &next);
  if (error)
    return error;
  out->first = first;
  out->next = next;
  out->offset = (u32) (offset % CACHE_BYTES);
  return 0;
}

static isize
reserve(Loadtable *table, u64 inode, const Loadfile *file, Loadslot **out)
{
  Loadslot *entry, *free = NULL;
  u32       i;
  for (i = 0; i < LOAD_SLOTS; i++) {
    entry = &table->slots[i];
    if (!entry->id) {
      if (!free)
        free = entry;
    } else if (entry->file.inode == inode) {
      if (!file || !entry->pid)
        return -ERR_BUSY;
      if (!samefile(&entry->file, file))
        return -ERR_STALE;
    }
  }
  if (!free)
    return -ERR_CAPACITY;
  if (table->serial == UINT64_MAX)
    return -ERR_OVERFLOW;
  free->id = ++table->serial;
  free->file.inode = inode;
  free->pid = 0;
  free->read = 0;
  free->stopped = 0;
  *out = free;
  return 0;
}

static void
stop(Loadslot *entry, isize error)
{
  if (!entry->read)
    entry->id = 0;
  else if (!entry->stopped)
    entry->stopped = error;
}

isize
loadopen(Loadtable *table, const Loadfile *file, Handle pid, u64 image, u64 now, u64 deadline,
         u64 *session)
{
  Loadslot *entry;
  isize     error;
  if (!table || !file || !file->inode || !file->generation || !file->version || pid <= 0 ||
      !image || !session)
    return -ERR_INVALID;
  if (deadline <= now)
    return -ERR_TIMEDOUT;
  error = reserve(table, file->inode, file, &entry);
  if (error)
    return error;
  entry->file = *file;
  entry->pid = pid;
  entry->image = image;
  entry->deadline = deadline;
  *session = entry->id;
  return 0;
}

isize
loadread(Loadtable *table, u64 session, Handle pid, u64 image, const Loadfile *file, u64 offset,
         u64 bytes, u64 now, u64 *ticket)
{
  Loadslot *entry = lookup(table, session);
  if (pid <= 0 || !image || !file || !ticket || !bytes || bytes > FS_SLOT_BYTES)
    return -ERR_INVALID;
  if (!entry || !entry->pid)
    return -ERR_STALE;
  if (entry->pid != pid || entry->image != image)
    return -ERR_DENIED;
  if (entry->stopped)
    return entry->stopped;
  if (now >= entry->deadline) {
    stop(entry, -ERR_TIMEDOUT);
    return -ERR_TIMEDOUT;
  }
  if (!samefile(&entry->file, file))
    return -ERR_STALE;
  if (offset > entry->file.length || bytes > entry->file.length - offset)
    return -ERR_INVALID;
  if (entry->read)
    return -ERR_BUSY;
  if (table->serial == UINT64_MAX)
    return -ERR_OVERFLOW;
  entry->read = ++table->serial;
  *ticket = entry->read;
  return 0;
}

static isize
readstatus(const Loadslot *entry, u64 ticket, u64 now)
{
  if (!entry || !ticket || entry->read != ticket)
    return -ERR_STALE;
  return entry->stopped ? entry->stopped : now >= entry->deadline ? -ERR_TIMEDOUT : 0;
}

isize
loadstatus(Loadtable *table, u64 session, u64 ticket, u64 now)
{
  return readstatus(lookup(table, session), ticket, now);
}

isize
loadfinish(Loadtable *table, u64 session, u64 ticket, u64 now)
{
  Loadslot *entry = lookup(table, session);
  isize     error = readstatus(entry, ticket, now);
  if (error == -ERR_STALE)
    return error;
  entry->read = 0;
  if (error)
    stop(entry, error);
  return error;
}

isize
loadclose(Loadtable *table, u64 session, Handle pid, u64 image)
{
  Loadslot *entry = lookup(table, session);
  if (!entry || !entry->pid)
    return -ERR_STALE;
  if (entry->pid != pid || entry->image != image)
    return -ERR_DENIED;
  stop(entry, -ERR_CANCELLED);
  return 0;
}

void
loadsweep(Loadtable *table, u64 now)
{
  u32 i;
  for (i = 0; i < LOAD_SLOTS; i++)
    if (table->slots[i].id && table->slots[i].pid && now >= table->slots[i].deadline)
      stop(&table->slots[i], -ERR_TIMEDOUT);
}

void
loadretire(Loadtable *table, Handle pid, u64 image)
{
  u32 i;
  if (pid <= 0 || !image)
    return;
  for (i = 0; i < LOAD_SLOTS; i++)
    if (table->slots[i].id && table->slots[i].pid == pid && table->slots[i].image == image)
      stop(&table->slots[i], -ERR_PEER);
}

isize
loadwrite(Loadtable *table, u64 inode, u64 *guard)
{
  Loadslot *entry;
  isize     error;
  if (!table || !inode || !guard)
    return -ERR_INVALID;
  error = reserve(table, inode, NULL, &entry);
  if (!error)
    *guard = entry->id;
  return error;
}

isize
loadunwrite(Loadtable *table, u64 guard)
{
  Loadslot *entry = lookup(table, guard);
  if (!entry || entry->pid)
    return -ERR_STALE;
  entry->id = 0;
  return 0;
}
