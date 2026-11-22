#include "load.h"
#include "txn.h"

static int
separate(const void *a, u64 bytes, const void *b, u64 length)
{
  uintptr_t x = (uintptr_t) a, y = (uintptr_t) b;
  return a && b && bytes <= UINT64_MAX - x && length <= UINT64_MAX - y &&
         (x + bytes <= y || y + length <= x);
}

isize
txnpreparefile(Txn *txn, const Loadmap *map, const Loadindex *index, void *snapshots, u32 slots)
{
  Cachekey keys[CACHE_MAX];
  Cache   *cache;
  u64      offset, room = (u64) slots * CACHE_BYTES;
  u32      i, j, count = 0;
  isize    error = 0;
  if (!txn || !(cache = txn->cache) || !cache->entries || !cache->capacity ||
      cache->capacity > CACHE_MAX || cache->blocks <= FS_DATA_START ||
      cache->blocks > FS_MAX_BLOCKS || !map || map->file.length > FS_FILE_BYTES ||
      slots > CACHE_MAX ||
      (slots && (!separate(map, sizeof(*map), snapshots, room) ||
                 (index && !separate(index, sizeof(*index), snapshots, room)))))
    return -ERR_INVALID;
  if (txn->phase != TXN_IDLE || txn->journal.phase != JOURNAL_IDLE || txn->count)
    return -ERR_STATE;
  for (offset = 0; offset < map->file.length; offset += CACHE_BYTES) {
    Loadspan span;
    error = loadmapspan(&map->file, map, index, FS_DATA_START, (u32) cache->blocks, offset,
                        MIN(map->file.length - offset, CACHE_BYTES), &span);
    if (error)
      break;
    if (!span.first)
      continue;
    for (i = 0; i < cache->capacity; i++)
      if (cache->entries[i].bound && cache->entries[i].block == span.first)
        break;
    if (i == cache->capacity || !cache->entries[i].dirty)
      continue;
    for (j = 0; j < count && keys[j].index != i; j++) {
    }
    if (j != count || cache->entries[i].metadata) {
      error = -ERR_STATE;
      break;
    }
    if (count == slots) {
      error = -ERR_CAPACITY;
      break;
    }
    keys[count] = (Cachekey){0};
    error = cacheget(cache, span.first, CACHE_DATA, &keys[count]);
    if (error)
      break;
    count++;
  }
  if (!error && count) {
    error = txnprepare(txn, NULL, 0, keys, count, snapshots);
    if (!error)
      error = 1;
  }
  while (count)
    if (cacheput(cache, &keys[--count]))
      error = -ERR_STATE;
  return error;
}
