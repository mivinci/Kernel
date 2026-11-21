#include "private/conn.h"
#include "private/syscall.h"
#include <abi/boot.h>
#include <abi/resource.h>
#include <proto/file.h>
static int
peerok(Connpeer peer)
{
  return handlekind(peer.pid) == HANDLE_PROCESS && peer.image;
}
static int
same(Connpeer a, Connpeer b)
{
  return a.pid == b.pid && a.image == b.image;
}
static int
connidok(u64 id)
{
  return id >> 16 && (id & 65535) < FS_CONN_COUNT;
}
static int
output(const Conncache *cache, const void *out, u64 bytes)
{
  u64 base = (u64) (uintptr_t) cache, address = (u64) (uintptr_t) out;
  return cache && out && sizeof(*cache) <= UINT64_MAX - base && bytes <= UINT64_MAX - address &&
         (base + sizeof(*cache) <= address || address + bytes <= base);
}
static int
valid(const Connentry *entry)
{
  if (entry->reserved || entry->state > SYSCONN_RETIRED)
    return 0;
  if (entry->state == SYSCONN_FREE || entry->state == SYSCONN_RETIRED)
    return !entry->peer.pid && !entry->peer.image && !entry->sequence && !entry->owner &&
           !entry->connid &&
           (entry->state == SYSCONN_RETIRED ? entry->generation == UINT64_MAX
                                            : entry->generation < UINT64_MAX);
  if (!entry->generation || !peerok(entry->peer))
    return 0;
  if (entry->state == SYSCONN_UNCLAIMED)
    return !entry->owner && !entry->connid;
  if (entry->state == SYSCONN_CONNECTING)
    return entry->sequence && entry->owner && !entry->connid;
  if (entry->state == SYSCONN_READY)
    return entry->sequence && connidok(entry->connid);
  return (!entry->owner || entry->sequence) &&
         (!entry->connid || (entry->sequence && connidok(entry->connid)));
}
static isize
entryat(Conncache *cache, Conntag tag, Connentry **out)
{
  Connentry *entry;
  if (!cache || !peerok(cache->self) || tag.reserved || tag.index >= SYSCONN_COUNT)
    return -ERR_INVALID;
  entry = &cache->entries[tag.index];
  if (!valid(entry))
    return -ERR_STATE;
  if (!tag.generation || entry->generation != tag.generation || entry->state == SYSCONN_FREE ||
      entry->state == SYSCONN_RETIRED)
    return -ERR_STALE;
  *out = entry;
  return 0;
}
isize
sysconnforkcheck(Conncache *cache, Connpeer parent, const Fixconn *entries, u64 count)
{
  u64   i, j;
  int   found;
  isize error = sysconncheck(cache, parent);
  if (error)
    return error;
  if (count > SYSCONN_COUNT || (count && !entries))
    return -ERR_INVALID;
  for (i = 0; i < count; i++) {
    if (handlekind(entries[i].pid) != HANDLE_PROCESS || !entries[i].image || !entries[i].token)
      return -ERR_INVALID;
    for (j = 0; j < i; j++)
      if (entries[i].pid == entries[j].pid && entries[i].image == entries[j].image)
        return -ERR_INVALID;
    found = 0;
    for (j = 0; j < SYSCONN_COUNT; j++) {
      Connentry *entry = &cache->entries[j];
      if (entry->peer.pid != entries[i].pid || entry->peer.image != entries[i].image)
        continue;
      if (entry->state == SYSCONN_DRAINING)
        return -ERR_STATE;
      if (entry->state == SYSCONN_UNCLAIMED || entry->state == SYSCONN_CONNECTING ||
          entry->state == SYSCONN_READY)
        found = 1;
    }
    if (!found)
      return -ERR_STALE;
  }
  return 0;
}
static isize
claimat(Conncache *cache, Connclaim claim, Connentry **out)
{
  isize error = entryat(cache, claim.tag, out);
  if (error)
    return error;
  if (!claim.seq || !claim.owner || (*out)->sequence != claim.seq || (*out)->owner != claim.owner)
    return -ERR_STALE;
  return 0;
}
isize
sysconnpeer(Handle call, Connpeer *out)
{
  Queryrec rows[QUERY_RECORDS];
  Connpeer peer;
  u64      cursor = 0, written, next;
  u32      i, batch;
  isize    error;
  if (!out)
    return -ERR_INVALID;
  if (handlekind(call) != HANDLE_RESOURCE)
    return -ERR_BAD_HANDLE;
  for (batch = 0; batch < (BOOT_RESOURCE_COUNT + QUERY_RECORDS - 1) / QUERY_RECORDS; batch++) {
    error = __sys_query(0, QUERY_RESOURCE, cursor, rows, QUERY_RECORDS, &written, &next);
    if (error)
      return error;
    if (written > QUERY_RECORDS || (next && (!written || next == cursor)))
      return -ERR_STATE;
    for (i = 0; i < written; i++)
      if ((Handle) rows[i].data[0] == call) {
        if (rows[i].type != QUERY_RESOURCE || rows[i].data[1] != RESOURCE_CALL)
          return -ERR_BAD_HANDLE;
        if (!(rows[i].data[2] & RESOURCE_USE))
          return -ERR_DENIED;
        peer.pid = (Handle) rows[i].data[6];
        peer.image = rows[i].data[7];
        if (!peerok(peer))
          return -ERR_STATE;
        out->pid = peer.pid;
        out->image = peer.image;
        return 0;
      }
    if (!next)
      return -ERR_STALE;
    cursor = next;
  }
  return -ERR_STATE;
}
isize
sysconnfork(Conncache *cache, Connpeer parent, Connpeer child, const Fixconn *entries, u64 count)
{
  u64   i, j;
  isize error;
  if (!cache || !peerok(parent) || !peerok(child) || parent.pid == child.pid ||
      count > SYSCONN_COUNT || (count && !output(cache, entries, count * sizeof(*entries))))
    return -ERR_INVALID;
  if (same(cache->self, child))
    return sysconncheck(cache, child);
  error = sysconnforkcheck(cache, parent, entries, count);
  if (error)
    return error;
  for (i = 0; i < SYSCONN_COUNT; i++)
    for (j = 0; j < count; j++)
      if (cache->entries[i].peer.pid == entries[j].pid &&
          cache->entries[i].peer.image == entries[j].image &&
          cache->entries[i].generation == UINT64_MAX)
        return -ERR_OVERFLOW;
  for (i = 0; i < SYSCONN_COUNT; i++) {
    Connentry *entry = &cache->entries[i];
    int        keep = 0;
    for (j = 0; j < count; j++)
      if (entry->peer.pid == entries[j].pid && entry->peer.image == entries[j].image)
        keep = 1;
    entry->sequence = entry->owner = entry->connid = 0;
    if (entry->generation != UINT64_MAX)
      entry->generation++;
    entry->state = keep                              ? SYSCONN_UNCLAIMED
                   : entry->generation == UINT64_MAX ? SYSCONN_RETIRED
                                                     : SYSCONN_FREE;
    if (!keep)
      entry->peer.pid = entry->peer.image = 0;
  }
  cache->sequence = 0;
  cache->self = child;
  return 0;
}
isize
sysconninit(Conncache *cache, Connpeer self)
{
  u32 i;
  if (!cache || !peerok(self))
    return -ERR_INVALID;
  cache->self.pid = self.pid;
  cache->self.image = self.image;
  cache->sequence = 0;
  for (i = 0; i < SYSCONN_COUNT; i++) {
    Connentry *entry = &cache->entries[i];
    entry->generation = entry->peer.pid = entry->peer.image = 0;
    entry->sequence = entry->owner = entry->connid = 0;
    entry->state = SYSCONN_FREE;
    entry->reserved = 0;
  }
  return 0;
}
isize
sysconncheck(const Conncache *cache, Connpeer self)
{
  u32 i, j;
  if (!cache || !peerok(self))
    return -ERR_INVALID;
  if (!same(cache->self, self))
    return -ERR_STALE;
  for (i = 0; i < SYSCONN_COUNT; i++) {
    const Connentry *entry = &cache->entries[i];
    if (!valid(entry) || entry->sequence > cache->sequence)
      return -ERR_STATE;
    if (entry->state == SYSCONN_FREE || entry->state == SYSCONN_RETIRED)
      continue;
    for (j = 0; j < i; j++)
      if (cache->entries[j].state != SYSCONN_FREE && cache->entries[j].state != SYSCONN_RETIRED &&
          same(entry->peer, cache->entries[j].peer))
        return -ERR_STATE;
  }
  return 0;
}
isize
sysconnbind(Conncache *cache, Connpeer peer, Conntag *out)
{
  u32   i, free = SYSCONN_COUNT;
  isize error;
  if (!peerok(peer) || !output(cache, out, sizeof(*out)))
    return -ERR_INVALID;
  error = sysconncheck(cache, cache->self);
  if (error)
    return error;
  for (i = 0; i < SYSCONN_COUNT; i++) {
    Connentry *entry = &cache->entries[i];
    if (entry->state == SYSCONN_FREE && free == SYSCONN_COUNT)
      free = i;
    if (entry->state == SYSCONN_FREE || entry->state == SYSCONN_RETIRED || !same(entry->peer, peer))
      continue;
    if (entry->state == SYSCONN_DRAINING)
      return -ERR_PEER;
    out->generation = entry->generation;
    out->index = i;
    out->reserved = 0;
    return 0;
  }
  if (free == SYSCONN_COUNT)
    return -ERR_CAPACITY;
  cache->entries[free].generation++;
  cache->entries[free].peer.pid = peer.pid;
  cache->entries[free].peer.image = peer.image;
  cache->entries[free].state = SYSCONN_UNCLAIMED;
  out->generation = cache->entries[free].generation;
  out->index = free;
  out->reserved = 0;
  return 0;
}
isize
sysconnview(Conncache *cache, Conntag tag, Connentry *out)
{
  Connentry *entry;
  isize      error;
  if (!output(cache, out, sizeof(*out)))
    return -ERR_INVALID;
  error = entryat(cache, tag, &entry);
  if (error)
    return error;
  out->generation = entry->generation;
  out->peer.pid = entry->peer.pid;
  out->peer.image = entry->peer.image;
  out->sequence = entry->sequence;
  out->owner = entry->owner;
  out->connid = entry->connid;
  out->state = entry->state;
  out->reserved = 0;
  return 0;
}
isize
sysconnbegin(Conncache *cache, Conntag tag, u64 owner, Connclaim *out)
{
  Connentry *entry;
  isize      error;
  if (!owner || !output(cache, out, sizeof(*out)))
    return -ERR_INVALID;
  error = entryat(cache, tag, &entry);
  if (error)
    return error;
  if (entry->state == SYSCONN_DRAINING)
    return -ERR_PEER;
  if (entry->state == SYSCONN_CONNECTING)
    return -ERR_BUSY;
  if (entry->state != SYSCONN_UNCLAIMED)
    return -ERR_STATE;
  if (cache->sequence == UINT64_MAX)
    return -ERR_OVERFLOW;
  entry->sequence = ++cache->sequence;
  entry->owner = owner;
  entry->state = SYSCONN_CONNECTING;
  out->tag.generation = tag.generation;
  out->tag.index = tag.index;
  out->tag.reserved = 0;
  out->seq = entry->sequence;
  out->owner = owner;
  return 0;
}
isize
sysconnready(Conncache *cache, Connclaim claim, u64 connid)
{
  Connentry *entry;
  isize      error;
  if (!connidok(connid))
    return -ERR_INVALID;
  error = claimat(cache, claim, &entry);
  if (error)
    return error;
  if (entry->state != SYSCONN_CONNECTING && entry->state != SYSCONN_READY)
    return -ERR_STALE;
  if (entry->state == SYSCONN_READY && entry->connid != connid)
    return -ERR_STALE;
  entry->connid = connid;
  entry->owner = 0;
  entry->state = SYSCONN_READY;
  return 0;
}
isize
sysconnrollback(Conncache *cache, Connclaim claim)
{
  Connentry *entry;
  isize      error = claimat(cache, claim, &entry);
  if (error)
    return error;
  if (entry->state != SYSCONN_CONNECTING && entry->state != SYSCONN_READY)
    return -ERR_STATE;
  entry->owner = 0;
  if (entry->state == SYSCONN_CONNECTING)
    entry->state = SYSCONN_UNCLAIMED;
  return 0;
}
isize
sysconnreusebegin(Conncache *cache, Conntag tag, u64 owner, Connclaim *out)
{
  Connentry *entry;
  isize      error;
  if (!owner || !output(cache, out, sizeof(*out)))
    return -ERR_INVALID;
  error = entryat(cache, tag, &entry);
  if (error)
    return error;
  if (entry->state == SYSCONN_DRAINING)
    return -ERR_PEER;
  if (entry->owner)
    return -ERR_BUSY;
  if (entry->state != SYSCONN_READY)
    return -ERR_STATE;
  if (cache->sequence == UINT64_MAX)
    return -ERR_OVERFLOW;
  entry->sequence = ++cache->sequence;
  entry->owner = owner;
  out->tag.generation = tag.generation;
  out->tag.index = tag.index;
  out->tag.reserved = 0;
  out->seq = entry->sequence;
  out->owner = owner;
  return 0;
}
isize
sysconnabortbegin(Conncache *cache, u64 owner, Connclaim *out)
{
  isize error;
  if (!owner || !output(cache, out, sizeof(*out)))
    return -ERR_INVALID;
  error = sysconncheck(cache, cache->self);
  if (error)
    return error;
  if (cache->sequence == UINT64_MAX)
    return -ERR_OVERFLOW;
  out->tag.generation = 0;
  out->tag.index = SYSCONN_COUNT;
  out->tag.reserved = 0;
  out->seq = ++cache->sequence;
  out->owner = owner;
  return 0;
}
isize
sysconninvalidate(Conncache *cache, Conntag tag)
{
  Connentry *entry;
  isize      error = entryat(cache, tag, &entry);
  if (error)
    return error;
  entry->state = SYSCONN_DRAINING;
  return 0;
}
isize
sysconncleaned(Conncache *cache, Connclaim claim)
{
  Connentry *entry;
  isize      error = claimat(cache, claim, &entry);
  if (error)
    return error;
  if (entry->state != SYSCONN_DRAINING)
    return -ERR_STATE;
  entry->owner = 0;
  return 0;
}
isize
sysconnforget(Conncache *cache, Conntag tag)
{
  Connentry *entry;
  isize      error = entryat(cache, tag, &entry);
  if (error)
    return error;
  if (entry->state != SYSCONN_DRAINING || entry->owner)
    return -ERR_BUSY;
  if (entry->generation != UINT64_MAX)
    entry->generation++;
  entry->peer.pid = entry->peer.image = entry->sequence = entry->connid = 0;
  entry->state = entry->generation == UINT64_MAX ? SYSCONN_RETIRED : SYSCONN_FREE;
  return 0;
}
