#include "load.h"
#include "sync.h"
#include <abi/ipc.h>
#include <errno.h>

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

isize
filesyncstep(Syncwork *work, Handle request, u64 state, const Loadmap *map, const Loadindex *index,
             isize (*submit)(void *, Handle, const Journalio *), void *context)
{
  Txn      *txn;
  Journalio io;
  isize     error, result;
  if (!work || !(txn = work->txn) || handlekind(request) != HANDLE_REQUEST || !submit)
    return -EINVAL;
  if (work->request != request) {
    if (state != RQ_CHECK_LIVE)
      return -EINTR;
    if (work->request || txn->phase == TXN_PREPARED || txn->phase == TXN_RUNNING)
      return 0;
    error = txnpreparefile(txn, map, index, work->snapshots, work->slots);
    if (!error)
      return 1;
    if (error == -ERR_BUSY)
      return 0;
    if (error < 0)
      return error == -ERR_CAPACITY ? -EAGAIN : -EIO;
    work->request = request;
    if (txnstart(txn, NULL, NULL, NULL))
      goto release;
  }
  while (txn->phase == TXN_RUNNING && !txn->journal.active && txn->journal.phase != JOURNAL_DONE &&
         txn->journal.phase != JOURNAL_STOPPED) {
    error = txnnext(txn, &io);
    if (error) {
      if (txnstop(txn))
        return 0;
      break;
    }
    if (submit(context, request, &io) && filesynccomplete(work, request, &io, 0))
      return 0;
  }
  if (txn->journal.active)
    return 0;
release:
  result = txn->journal.phase == JOURNAL_DONE ? 1 : -EIO;
  if (txnrelease(txn))
    return 0;
  work->request = 0;
  return result;
}

isize
filesynccomplete(Syncwork *work, Handle request, const Journalio *io, u32 success)
{
  if (!work || !request || work->request != request)
    return -ERR_STALE;
  return txncomplete(work->txn, io, success);
}
