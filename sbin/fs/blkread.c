#include "blkread.h"
#include "journal.h"

STATIC_ASSERT(CACHE_BYTES <= BLK_MAX_IO, block_read_fits_slot);

isize
blkreadstart(Blkread *read, Cache *cache, u64 block, u32 metadata)
{
  Cacheio ticket;
  u8     *destination;
  isize   error;
  if (!read || !read->blk || !cache || block > UINT64_MAX / CACHE_BYTES)
    return -ERR_INVALID;
  if (read->fill.cache || read->blk->rpc)
    return -ERR_WOULD_BLOCK;
  error = cachefillstart(&read->fill, cache, block, metadata, &ticket, &destination);
  if (error != 1)
    return error;
  error = fsblksubmit(read->blk, BLK_READ, block * CACHE_BYTES, CACHE_BYTES, NULL);
  if (error) {
    isize release = cachefillfinish(&read->fill, &ticket, 0);
    return release ? release : error;
  }
  return 1;
}

isize
blkreadpoll(Blkread *read, isize *status)
{
  Cacheio ticket;
  isize   error;
  if (!read || !read->fill.cache || !status)
    return -ERR_INVALID;
  if (!read->collected) {
    error = fsblkpoll(read->blk, read->data, sizeof(read->data), &read->status);
    if (error)
      return error;
    read->collected = 1;
  }
  ticket = read->fill.ticket;
  error = read->status ? cachefillfinish(&read->fill, &ticket, 0)
                       : cachefillcopy(&read->fill, &ticket, read->data, sizeof(read->data));
  if (!error) {
    *status = read->status;
    read->collected = 0;
  }
  return error;
}

isize
blkreadview(Blkread *read, Cache *cache, const Redoscan *scan, u64 block, u32 metadata, void *out)
{
  Cachekey  key = {0};
  const u8 *source;
  u64       version;
  u32       i;
  isize     error, release;
  if (!read || !read->blk || !cache || !scan || cache->blocks != scan->blocks || !out ||
      metadata > CACHE_META)
    return -ERR_INVALID;
  if (scan->failed)
    return scan->failed;
  error = journalview(scan->blocks, scan->images, scan->count, block, &source);
  if (error)
    return error;
  if (!source) {
    error = blkreadstart(read, cache, block, metadata);
    if (error)
      return error == 1 ? -ERR_WOULD_BLOCK : error;
    error = cacheget(cache, block, metadata, &key);
    if (error)
      return error;
    error = cacheview(cache, &key, &source, &version);
    if (error) {
      release = cacheput(cache, &key);
      return release ? release : error;
    }
  }
  for (i = 0; i < CACHE_BYTES; i++)
    ((u8 *) out)[i] = source[i];
  return key.generation ? cacheput(cache, &key) : 0;
}
