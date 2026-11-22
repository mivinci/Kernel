#include "load.h"
#include "cache.h"
#include "geometry.h"
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
      bytes > FS_SLOT_BYTES || file->length > FS_FILE_BYTES || offset > file->length ||
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

isize
loadmapwrite(const Loadmap *map, const Loadindex *index, u32 start, u32 blocks, u64 offset,
             u64 bytes, Writeplan *out)
{
  Writeplan plan = {0};
  u64       length;
  isize     error;
  if (!map || !out || !start || start >= blocks || !bytes || bytes > FS_SLOT_BYTES ||
      map->file.length > FS_FILE_BYTES)
    return -ERR_INVALID;
  if (offset >= FS_FILE_BYTES)
    return -ERR_OVERFLOW;
  if (map->indirect && (map->indirect < start || map->indirect >= blocks))
    return -ERR_STATE;
  length = map->file.length;
  plan.logical = (u32) (offset / CACHE_BYTES);
  plan.offset = (u32) (offset % CACHE_BYTES);
  plan.bytes = (u32) MIN(bytes, CACHE_BYTES - plan.offset);
  if (offset < length)
    plan.bytes = (u32) MIN(plan.bytes, length - offset);
  plan.length = MAX(length, offset + plan.bytes);
  error = datablock(map, index, start, blocks, plan.logical, &plan.block);
  if (error)
    return error;
  /* An allocated whole block beyond EOF may still belong to truncate. */
  if (plan.block && (u64) plan.logical * CACHE_BYTES >= length)
    return -ERR_BUSY;
  plan.newindex = plan.logical >= LOAD_DIRECT && !map->indirect;
  if (offset > length && length % CACHE_BYTES) {
    u32 logical = (u32) (length / CACHE_BYTES);
    error = datablock(map, index, start, blocks, logical, &plan.tail);
    if (error)
      return error;
    if (plan.tail) {
      plan.zerooff = (u32) (length % CACHE_BYTES);
      plan.zerobytes = (u32) MIN(offset - length, CACHE_BYTES - plan.zerooff);
    }
  }
  *out = plan;
  return 0;
}

static int
separate(const void *a, usize bytes, const void *b, usize count)
{
  uintptr_t x = (uintptr_t) a, y = (uintptr_t) b;
  return a && b && bytes <= UINT64_MAX - x && count <= UINT64_MAX - y &&
         (x + bytes <= y || y + count <= x);
}

isize
loadwritestage(const Writeplan *plan, u32 start, u32 blocks, u32 reserved, const void *olddata,
               const void *oldtail, const void *input, Writeimage *out)
{
  const u8 *data = olddata, *tail = oldtail, *source = input;
  u32       block, tailblock, i;
  if (!out || !separate(plan, sizeof(*plan), out, sizeof(*out)) || !start || start >= blocks ||
      !plan->bytes || plan->offset >= FS_BLOCK_BYTES ||
      plan->bytes > FS_BLOCK_BYTES - plan->offset ||
      (plan->block ? reserved || !olddata : !reserved || olddata) ||
      !separate(input, plan->bytes, out, sizeof(*out)) ||
      (olddata && !separate(olddata, FS_BLOCK_BYTES, out, sizeof(*out))))
    return -ERR_INVALID;
  block = plan->block ? plan->block : reserved;
  tailblock = plan->tail == plan->block ? 0 : plan->tail;
  if (block < start || block >= blocks ||
      (tailblock && (tailblock < start || tailblock >= blocks || tailblock == block)) ||
      (!!tailblock != !!oldtail) ||
      (oldtail && !separate(oldtail, FS_BLOCK_BYTES, out, sizeof(*out))) ||
      (!!plan->tail != !!plan->zerobytes) || plan->zerooff >= FS_BLOCK_BYTES ||
      plan->zerobytes > FS_BLOCK_BYTES - plan->zerooff ||
      (plan->tail && !tailblock && plan->zerooff + plan->zerobytes > plan->offset))
    return -ERR_INVALID;
  for (i = 0; i < FS_BLOCK_BYTES; i++)
    out->data[i] = data ? data[i] : 0;
  if (tailblock)
    for (i = 0; i < FS_BLOCK_BYTES; i++)
      out->taildata[i] = tail[i];
  for (i = plan->zerooff; i < plan->zerooff + plan->zerobytes; i++)
    (tailblock ? out->taildata : out->data)[i] = 0;
  for (i = 0; i < plan->bytes; i++)
    out->data[plan->offset + i] = source[i];
  out->block = block;
  out->tail = tailblock;
  return 0;
}

static isize
writeupdate(const Loadmap *map, const Loadindex *index, u32 start, u32 blocks,
            const Writeplan *plan, u32 data, u32 indirect, Writeupdate *out)
{
  u32 i, indexdirty;
  if (!separate(map, sizeof(*map), out, sizeof(*out)) ||
      (index && !separate(index, sizeof(*index), out, sizeof(*out))))
    return -ERR_INVALID;
  if (!map->file.version || map->file.version == UINT64_MAX)
    return -ERR_OVERFLOW;
  if ((!!data == !!plan->block) || (!!indirect != !!plan->newindex) ||
      (data && (data < start || data >= blocks || data == map->indirect)) ||
      (indirect && (indirect < start || indirect >= blocks || indirect == data)))
    return -ERR_INVALID;
  if (data && map->indirect) {
    if (!index)
      return -ERR_WOULD_BLOCK;
    if (index->block != map->indirect)
      return -ERR_STALE;
    for (i = 0; i < LOAD_INDIRECT; i++)
      if (index->entries[i] == data)
        return -ERR_STATE;
  }
  if (data)
    for (i = 0; i < LOAD_DIRECT; i++)
      if (map->direct[i] == data || (indirect && map->direct[i] == indirect))
        return -ERR_STATE;
  indexdirty = data && plan->logical >= LOAD_DIRECT;
  out->plan = *plan;
  out->map = *map;
  out->map.file.version++;
  out->map.file.length = plan->length;
  out->indexdirty = indexdirty;
  if (indexdirty) {
    if (indirect) {
      out->map.indirect = indirect;
      out->index.block = indirect;
      for (i = 0; i < LOAD_INDIRECT; i++)
        out->index.entries[i] = 0;
    } else
      out->index = *index;
    out->index.entries[plan->logical - LOAD_DIRECT] = data;
  } else if (data)
    out->map.direct[plan->logical] = data;
  return 0;
}

isize
loadwriteupdate(const Loadmap *map, const Loadindex *index, u32 start, u32 blocks, u64 offset,
                u64 bytes, u32 data, u32 indirect, Writeupdate *out)
{
  Writeplan plan;
  isize     error = loadmapwrite(map, index, start, blocks, offset, bytes, &plan);
  return error ? error : writeupdate(map, index, start, blocks, &plan, data, indirect, out);
}

static u64
blockbit(u32 block)
{
  return UINT64_C(1) << (block % 64);
}

static int
blockused(const Blockmap *allocation, u32 block)
{
  return !!(allocation->used[block / 64] & blockbit(block));
}

isize
loadwritealloc(const Loadmap *map, const Loadindex *index, u32 start, u32 blocks, u64 offset,
               u64 bytes, Blockmap *allocation, Writeupdate *out)
{
  Writeplan plan;
  u32       data = 0, indirect = 0, i;
  isize     error;
  if (blocks > FS_MAX_BLOCKS || !separate(allocation, sizeof(*allocation), out, sizeof(*out)) ||
      !separate(allocation, sizeof(*allocation), map, sizeof(*map)) ||
      (index && !separate(allocation, sizeof(*allocation), index, sizeof(*index))))
    return -ERR_INVALID;
  error = loadmapwrite(map, index, start, blocks, offset, bytes, &plan);
  if (error)
    return error;
  if ((plan.block && !blockused(allocation, plan.block)) ||
      (plan.tail && !blockused(allocation, plan.tail)) ||
      (map->indirect && !blockused(allocation, map->indirect)))
    return -ERR_STATE;
  if (!plan.block) {
    for (i = start; i < blocks; i++) {
      if (blockused(allocation, i))
        continue;
      if (!data) {
        data = i;
        if (!plan.newindex)
          break;
      } else {
        indirect = i;
        break;
      }
    }
    if (!data || (plan.newindex && !indirect))
      return -ERR_CAPACITY;
  }
  error = writeupdate(map, index, start, blocks, &plan, data, indirect, out);
  if (error)
    return error;
  if (data)
    allocation->used[data / 64] |= blockbit(data);
  if (indirect)
    allocation->used[indirect / 64] |= blockbit(indirect);
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
