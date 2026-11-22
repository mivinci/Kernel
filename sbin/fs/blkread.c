#include "blkread.h"
#include "journal.h"

STATIC_ASSERT(CACHE_BYTES <= BLK_MAX_IO, block_read_fits_slot);
STATIC_ASSERT(CACHE_BYTES == FS_BLOCK_BYTES, block_read_matches_volume);

isize
blkreadboot(Fsblk *blk, u32 block)
{
  if (block > JOURNAL_MARK_BLOCK)
    return -ERR_INVALID;
  return fsblksubmit(blk, BLK_READ, (u64) block * FS_BLOCK_BYTES, FS_BLOCK_BYTES, NULL);
}

enum
{
  ProbeSuper,
  ProbeMark,
  ProbeDone
};

isize
blkprobestep(Blkprobe *probe, isize (*layout)(const u8 *, Fslayout *, u8 *),
             isize (*mark)(const u8 *, const u8 *, Journalmark *))
{
  Fsblk   *blk;
  Blkinfo *info;
  isize    error, status;
  if (!probe || !probe->blk || !layout || !mark)
    return -ERR_INVALID;
  blk = probe->blk;
  info = &blk->info;
  if (probe->failed)
    return probe->failed;
  if (probe->sequence) {
    if (!blk->rpc || blk->seq != probe->sequence || blk->opcode != BLK_READ)
      return probe->failed = -ERR_STATE;
    error = fsblkpoll(blk, probe->bytes, sizeof(probe->bytes), &status);
    if (error)
      return error == -ERR_WOULD_BLOCK ? 0 : error;
    probe->sequence = 0;
    if (status)
      return probe->failed = blk->stale ? -ERR_STALE : -ERR_STATE;
    if (blk->stale || info->geometry_generation != probe->generation)
      return probe->failed = -ERR_STALE;
    if (probe->phase == ProbeSuper) {
      error = layout(probe->bytes, &probe->layout, probe->uuid);
      if (!error)
        error = fslayoutcheck(&probe->layout, &probe->geometry, 0);
      if (error)
        return probe->failed = error < 0 ? error : -ERR_STATE;
      probe->phase = ProbeMark;
    } else {
      if (mark(probe->bytes, probe->uuid, &probe->mark) || probe->mark.state > 1 ||
          (probe->mark.state ? !probe->mark.txid : probe->mark.headercrc || probe->mark.payloadcrc))
        return probe->failed = -ERR_STATE;
      probe->phase = ProbeDone;
    }
  }
  if (blk->stale || !info->geometry_generation ||
      (probe->generation && info->geometry_generation != probe->generation))
    return probe->failed = -ERR_STALE;
  if (probe->phase == ProbeDone)
    return 1;
  if (blk->rpc)
    return -ERR_BUSY;
  if (!probe->generation) {
    if (info->logical_bytes > UINT32_MAX || info->physical_bytes > UINT32_MAX ||
        info->alignment_bytes > UINT32_MAX)
      return probe->failed = -ERR_NOTSUP;
    probe->generation = info->geometry_generation;
    probe->geometry = (Fsgeometry){info->capacity_bytes,
                                   info->logical_bytes,
                                   info->physical_bytes,
                                   info->alignment_bytes,
                                   !!(info->flags & BLK_READONLY),
                                   !!(info->flags & BLK_FLUSHABLE),
                                   0,
                                   0};
  }
  error = blkreadboot(blk, probe->phase == ProbeSuper ? 0 : JOURNAL_MARK_BLOCK);
  if (!error)
    probe->sequence = blk->seq;
  return error;
}

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

static isize
blkreadpart(Blkread *read, Cache *cache, const Redoscan *scan, u64 block, u32 metadata, u32 offset,
            u32 bytes, void *out)
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
  for (i = 0; i < bytes; i++)
    ((u8 *) out)[i] = source[offset + i];
  return key.generation ? cacheput(cache, &key) : 0;
}

isize
blkreadview(Blkread *read, Cache *cache, const Redoscan *scan, u64 block, u32 metadata, void *out)
{
  return blkreadpart(read, cache, scan, block, metadata, 0, CACHE_BYTES, out);
}

isize
blkreadinode(Blkread *read, Cache *cache, const Redoscan *scan, u32 inode, void *out)
{
  const u32 slots = FS_BLOCK_BYTES / FS_INODE_BYTES;
  if (!inode || inode >= FS_INODES)
    return -ERR_INVALID;
  return blkreadpart(read, cache, scan, FS_INODE_START + inode / slots, CACHE_META,
                     (inode % slots) * FS_INODE_BYTES, FS_INODE_BYTES, out);
}

isize
blkredoload(void *context, const Redoscan *scan, u32 ino, Redoinode *out)
{
  Blkredoload *load = context;
  isize        error;
  u32          block;
  if (!load || !scan || !scan->allocation || !out || !load->inode || !load->indirect || !ino ||
      scan->next != ino || (load->pending && load->pending != ino))
    return -ERR_INVALID;
  if (!load->pending) {
    error = blkreadinode(load->read, load->cache, scan, ino, load->bytes);
    if (error)
      return error;
    if (load->inode(ino, load->bytes, &load->map, &load->kind, &load->links))
      return -ERR_STATE;
    load->pending = ino;
  }
  block = load->map.indirect;
  if (block) {
    if (block < FS_DATA_START || block >= scan->blocks ||
        !(scan->allocation->used[block / 64] & (UINT64_C(1) << (block % 64))))
      return -ERR_STATE;
    error = blkreadview(load->read, load->cache, scan, block, CACHE_META, load->bytes);
    if (error)
      return error;
    if (load->indirect(block, load->bytes, &load->index))
      return -ERR_STATE;
  }
  *out = (Redoinode){&load->map, block ? &load->index : NULL, load->kind, load->links};
  load->pending = 0;
  return 0;
}
