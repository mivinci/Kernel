#include "blkread.h"
#include "crc.h"
#include "mount.h"

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
blkredostep(Blkredo *redo)
{
  const Blkprobe *probe;
  Fsblk          *blk;
  u8             *bytes;
  isize           error, status;
  u32             i;
  if (!redo || !(probe = redo->probe) || probe->phase != ProbeDone || !probe->blk)
    return -ERR_INVALID;
  if (redo->failed)
    return redo->failed;
  blk = probe->blk;
  if (redo->sequence) {
    if (!blk->rpc || blk->seq != redo->sequence || blk->opcode != BLK_READ)
      return redo->failed = -ERR_STATE;
    bytes = redo->bytes[redo->read ? redo->read - 1 : 0];
    error = fsblkpoll(blk, bytes, FS_BLOCK_BYTES, &status);
    if (error)
      return error == -ERR_WOULD_BLOCK ? 0 : error;
    redo->sequence = 0;
    if (blk->stale || blk->info.geometry_generation != probe->generation)
      return redo->failed = -ERR_STALE;
    if (status)
      return redo->failed = -ERR_STATE;
    if (!redo->read) {
      if (redo->decode(bytes, &redo->head) ||
          !journalheadvalid(probe->layout.blocks, &probe->mark, &redo->head))
        return redo->failed = -ERR_STATE;
      redo->crc = redo->initial;
    } else {
      redo->crc = fscrc32c(redo->crc, bytes, FS_BLOCK_BYTES);
    }
    redo->read++;
    if (redo->read == redo->head.count + 1) {
      if ((redo->crc ^ redo->finalxor) != probe->mark.payloadcrc)
        return redo->failed = -ERR_STATE;
      for (i = 0; i < redo->head.count; i++)
        redo->images[i] = (Journalblock){redo->head.targets[i], redo->bytes[i]};
    }
  }
  if (blk->stale || blk->info.geometry_generation != probe->generation)
    return redo->failed = -ERR_STALE;
  if (!probe->mark.state || (redo->read && redo->read == redo->head.count + 1))
    return 1;
  if (!redo->decode)
    return -ERR_INVALID;
  if (blk->rpc)
    return -ERR_BUSY;
  error = blkreadboot(blk, FS_LOG_START + redo->read);
  if (!error)
    redo->sequence = blk->seq;
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

enum
{
  OwnersRedo,
  OwnersInodes,
  OwnersBlocks,
  OwnersScan,
  OwnersDone
};

isize
blkownersstep(Blkowners *work)
{
  Blkredoload    *load;
  const Blkprobe *probe;
  Redoscan       *scan;
  isize           error, status;
  u32             i;
  if (!work || !!work->redo == !!work->home || !work->inodemap || !work->blockmap)
    return -ERR_INVALID;
  probe = work->redo ? work->redo->probe : work->home;
  if (!probe || !probe->blk || probe->phase != ProbeDone || (work->home && probe->mark.state))
    return -ERR_INVALID;
  load = &work->load;
  scan = &work->scan;
  if (!load->read || !load->cache || !load->inode || !load->indirect ||
      load->read->blk != probe->blk || load->cache->blocks != probe->layout.blocks)
    return -ERR_INVALID;
  if (work->failed)
    return work->failed;
  if (work->phase == OwnersRedo) {
    if (work->redo) {
      error = blkredostep(work->redo);
      if (error != 1)
        return error;
      scan->count = work->redo->head.count;
      for (i = 0; i < scan->count; i++)
        scan->images[i] = work->redo->images[i];
    }
    scan->blocks = (u32) probe->layout.blocks;
    work->phase = OwnersInodes;
  }
  if (load->read->fill.cache) {
    error = blkreadpoll(load->read, &status);
    if (error)
      return error == -ERR_WOULD_BLOCK ? 0 : error;
    if (status)
      return work->failed = probe->blk->stale ? -ERR_STALE : -ERR_STATE;
  }
  if (probe->blk->stale || probe->blk->info.geometry_generation != probe->generation)
    return work->failed = -ERR_STALE;
  if (work->phase == OwnersDone)
    return 1;
  if (work->phase < OwnersScan) {
    u32 block = work->phase == OwnersInodes ? FS_INODE_MAP : FS_BLOCK_MAP;
    error = blkreadview(load->read, load->cache, scan, block, CACHE_META, load->bytes);
    if (error == -ERR_WOULD_BLOCK)
      return 0;
    if (!error)
      error = work->phase == OwnersInodes
                  ? work->inodemap(load->bytes, &work->inodes)
                  : work->blockmap(load->bytes, scan->blocks, &work->allocation);
    if (error)
      return work->failed = error < 0 ? error : -ERR_STATE;
    if (++work->phase < OwnersScan)
      return 0;
    error = redoscaninit(scan, scan->blocks, work->redo ? work->redo->images : NULL, scan->count,
                         &work->inodes, &work->allocation, &work->owners);
  } else {
    error = redoscanstep(scan, blkredoload, load);
    if (error == -ERR_WOULD_BLOCK)
      return 0;
    if (error == 1)
      work->phase = OwnersDone;
  }
  if (error < 0)
    work->failed = error;
  return error;
}

isize
blkhomecheck(Blkowners *work, const Blkprobe *before, const Journal *retired)
{
  const Blkprobe *home;
  isize           error;
  u32             i;
  if (!work || work->redo || !(home = work->home) || !!before != !!retired)
    return -ERR_INVALID;
  if (home->phase != ProbeDone || home->failed || home->mark.state)
    return -ERR_STATE;
  if (before) {
    if (before->phase != ProbeDone || before->failed || before->mark.state != 1 ||
        retired->phase != JOURNAL_IDLE || retired->active || retired->txid ||
        retired->last != before->mark.txid || retired->blocks != before->layout.blocks)
      return -ERR_STATE;
    if (home->blk != before->blk || home->generation != before->generation ||
        home->layout.blocks != before->layout.blocks || home->mark.txid != retired->last)
      return -ERR_STALE;
    for (i = 0; i < sizeof(home->uuid); i++)
      if (home->uuid[i] != before->uuid[i])
        return -ERR_STALE;
  }
  error = blkownersstep(work);
  if (error != 1)
    return error;
  error = journalreadonly(&home->layout, &home->geometry, &home->mark, &work->scan);
  return error ? error : 1;
}

isize
blkredoadmit(const Blkowners *work, Journal *journal, u32 faultbytes, u32 faultoffset,
             const u8 *empty)
{
  const Blkredo  *redo;
  const Blkprobe *probe;
  Fsgeometry      geometry;
  if (!work || !(redo = work->redo) || !(probe = redo->probe) || !journal)
    return -ERR_INVALID;
  if (work->failed || redo->failed)
    return work->failed ? work->failed : redo->failed;
  if (work->phase != OwnersDone || probe->blk->rpc)
    return -ERR_WOULD_BLOCK;
  if (probe->blk->stale || probe->blk->info.geometry_generation != probe->generation)
    return -ERR_STALE;
  geometry = probe->geometry;
  geometry.faultbytes = faultbytes;
  geometry.faultoffset = faultoffset;
  return journalrecover(journal, &probe->layout, &geometry, &probe->mark, &redo->head, redo->images,
                        redo->head.count, redo->crc ^ redo->finalxor, empty, &work->scan);
}

static isize
loadinode(Blkredoload *load, const Redoscan *scan, u32 ino, Redoinode *out)
{
  isize error;
  u32   block;
  if (!load || !scan || !scan->allocation || !out || !load->inode || !load->indirect || !ino ||
      (load->pending && load->pending != ino))
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

isize
blkredoload(void *context, const Redoscan *scan, u32 ino, Redoinode *out)
{
  Blkredoload *load = context;
  isize        error;
  if (!scan || scan->next != ino)
    return -ERR_INVALID;
  error = loadinode(load, scan, ino, out);
  if (!error && load->nodes) {
    if (!load->map.file.generation || load->map.file.generation > UINT32_MAX)
      return -ERR_STATE;
    load->nodes[ino] = (Dirnode){load->map.file.generation, load->kind, load->links};
  }
  return error;
}

isize
blkdirstep(Blkdir *work)
{
  Blkowners      *home;
  Blkredoload    *load;
  const Blkprobe *probe;
  const Dirnode  *node;
  Loadspan        span;
  Redoinode       inode;
  isize           error, status;
  u32             valid, end;
  if (!work || !(home = work->home) || !(probe = home->home) || !work->decode ||
      home->phase != OwnersDone || home->failed || !home->load.nodes)
    return -ERR_INVALID;
  if (work->failed)
    return work->failed;
  load = &home->load;
  if (load->read->fill.cache) {
    error = blkreadpoll(load->read, &status);
    if (error)
      return error == -ERR_WOULD_BLOCK ? 0 : error;
    if (status)
      return work->failed = probe->blk->stale ? -ERR_STALE : -ERR_STATE;
  }
  if (probe->blk->stale || probe->blk->info.geometry_generation != probe->generation)
    return work->failed = -ERR_STALE;
  if (!work->inode) {
    work->graph.nodes = load->nodes;
    work->inode = FS_ROOT;
  }
  if (work->inode == FS_INODES)
    return dirgraphstep(&work->graph);
  end = MIN(work->inode + FS_BLOCK_BYTES / FS_INODE_BYTES, FS_INODES);
  while (work->inode < end) {
    node = &load->nodes[work->inode];
    if (node->kind == VNODE_DIR && node->links)
      break;
    work->inode++;
  }
  if (work->inode == end)
    return 0;
  node = &load->nodes[work->inode];
  if (!work->loaded) {
    error = loadinode(load, &home->scan, work->inode, &inode);
    if (error == -ERR_WOULD_BLOCK)
      return 0;
    if (error)
      return work->failed = error;
    if (inode.kind != node->kind || inode.links != node->links ||
        inode.map->file.inode != work->inode || inode.map->file.generation != node->generation)
      return work->failed = -ERR_STATE;
    work->loaded = 1;
  }
  if (work->offset == load->map.file.length) {
    error = dirnamesstep(&work->names);
    if (error != 1)
      return error < 0 ? (work->failed = error) : 0;
    work->inode++;
    work->loaded = 0;
    work->offset = 0;
    work->names.used = work->names.count = work->names.heap = work->names.next = 0;
    return 0;
  }
  valid = (u32) MIN(load->map.file.length - work->offset, FS_BLOCK_BYTES);
  error = loadmapspan(&load->map.file, &load->map, load->map.indirect ? &load->index : NULL,
                      FS_DATA_START, home->scan.blocks, work->offset, valid, &span);
  if (error || !span.first)
    return work->failed = -ERR_STATE;
  error = blkreadview(load->read, load->cache, &home->scan, span.first, CACHE_META, load->bytes);
  if (error == -ERR_WOULD_BLOCK)
    return 0;
  if (!error)
    error =
        dirgraphblock(&work->graph, &work->names, work->inode, load->bytes, valid, work->decode);
  if (error)
    return work->failed = error < 0 ? error : -ERR_STATE;
  work->offset += valid;
  return 0;
}

isize
blkmountadd(Blkdir *work, Vmounts *mounts, Vnode *root, Vnode *point)
{
  const Blkprobe *probe;
  const Dirnode  *node;
  isize           error;
  if (!work || !work->home || !(probe = work->home->home) || !probe->blk || !mounts || !root)
    return -ERR_INVALID;
  if (work->failed || work->graph.failed)
    return work->failed ? work->failed : work->graph.failed;
  if (work->published)
    return -ERR_STATE;
  if (work->inode != FS_INODES || work->graph.next != FS_INODES || probe->blk->rpc)
    return -ERR_WOULD_BLOCK;
  if (probe->blk->stale || probe->blk->ended ||
      probe->blk->info.geometry_generation != probe->generation)
    return -ERR_STALE;
  node = &work->graph.nodes[FS_ROOT];
  if (root->number != FS_ROOT || root->generation != node->generation || root->kind != node->kind)
    return -ERR_STALE;
  error = vmountadd(mounts, root, point);
  if (!error)
    work->published = 1;
  return error;
}

enum
{
  MountProbe,
  MountOverlay,
  MountReplay,
  MountReprobe,
  MountHome,
  MountDirectories
};

static isize
mountdrain(Blkmount *work, Fsblk *blk)
{
  Blkowners *owner = work->phase == MountOverlay ? work->overlay : work->dirs->home;
  Blkread   *read = owner ? owner->load.read : NULL;
  Journal   *journal = work->log.journal;
  isize      error, status;
  if (work->log.ticket.serial) {
    error = blklogpoll(&work->log);
    if (error)
      return error == -ERR_WOULD_BLOCK ? 0 : error;
  } else if (blk->rpc) {
    u8 *discard = blk->opcode == BLK_READ ? work->probe->bytes : NULL;
    error = fsblkpoll(blk, discard, discard ? FS_BLOCK_BYTES : 0, &status);
    if (error)
      return error == -ERR_WOULD_BLOCK ? 0 : error;
  }
  if (read && read->fill.cache) {
    Cacheio ticket = read->fill.ticket;
    error = cachefillfinish(&read->fill, &ticket, 0);
    if (error)
      return error;
    read->collected = 0;
  }
  if (work->phase == MountReplay && journal && journal->txid &&
      (journal->phase == JOURNAL_STOPPED || journal->phase == JOURNAL_DONE)) {
    error = journalrelease(journal);
    if (error)
      return error;
  }
  return work->failed;
}

isize
blkmountstop(Blkmount *work, isize reason)
{
  Journal *journal;
  if (!work || !work->probe || !work->probe->blk || !work->dirs || reason >= 0)
    return -ERR_INVALID;
  if (work->dirs->published)
    return -ERR_STATE;
  if (!work->stopped) {
    journal = work->log.journal;
    if (work->phase == MountReplay && journal && journal->phase != JOURNAL_DONE &&
        journal->phase != JOURNAL_IDLE)
      journalstop(journal);
    if (!work->failed)
      work->failed = reason;
    work->dirs->failed = work->failed;
    work->stopped = 1;
  }
  return mountdrain(work, work->probe->blk);
}

isize
blkmountstep(Blkmount *work)
{
  Blkprobe  *probe, *after;
  Blkowners *home;
  Fsblk     *blk;
  Journal   *journal;
  isize      result;
  if (!work || !(probe = work->probe) || !(blk = probe->blk) || !work->dirs ||
      !(home = work->dirs->home) || !work->storage || !work->layout || !work->mark)
    return -ERR_INVALID;
  if (work->stopped)
    return mountdrain(work, blk);
  if (work->failed)
    return work->failed;
  after = work->after;
  journal = work->log.journal;
  switch (work->phase) {
  case MountProbe:
    result = blkprobestep(probe, work->layout, work->mark);
    if (result != 1)
      break;
    if (!probe->mark.state) {
      home->home = probe;
      result = cacheinit(home->load.cache, work->storage, work->capacity, probe->layout.blocks);
      work->phase = MountHome;
      break;
    }
    if (!after || after == probe || !journal || work->log.blk != blk || !work->empty ||
        !work->retire || !work->overlay || work->overlay == home || !work->overlay->redo ||
        work->overlay->redo->probe != probe)
      return work->failed = -ERR_INVALID;
    {
      Fsgeometry geometry = probe->geometry;
      geometry.faultbytes = work->faultbytes;
      geometry.faultoffset = work->faultoffset;
      result = fslayoutcheck(&probe->layout, &geometry, 1);
    }
    if (result)
      break;
    result =
        cacheinit(work->overlay->load.cache, work->storage, work->capacity, probe->layout.blocks);
    work->phase = MountOverlay;
    break;
  case MountOverlay:
    result = blkownersstep(work->overlay);
    if (result != 1)
      break;
    result = work->retire(probe->uuid, probe->mark.txid, work->empty);
    if (result)
      return work->failed = result < 0 ? result : -ERR_STATE;
    result = blkredoadmit(work->overlay, journal, work->faultbytes, work->faultoffset, work->empty);
    work->phase = MountReplay;
    break;
  case MountReplay:
    if (work->log.ticket.serial) {
      result = blklogpoll(&work->log);
      if (result == -ERR_WOULD_BLOCK)
        return 0;
      break;
    }
    if (blk->stale || blk->ended || blk->info.geometry_generation != probe->generation) {
      journalstop(journal);
      return work->failed = -ERR_STALE;
    }
    result = journalpump(journal, blklogsubmit, &work->log);
    if (result != 1)
      break;
    result = journalrelease(journal);
    after->blk = blk;
    work->phase = MountReprobe;
    break;
  case MountReprobe:
    result = blkprobestep(after, work->layout, work->mark);
    if (result != 1)
      break;
    result = cacheinit(home->load.cache, work->storage, work->capacity, probe->layout.blocks);
    home->home = after;
    work->phase = MountHome;
    break;
  case MountHome:
    result =
        blkhomecheck(home, probe->mark.state ? probe : NULL, probe->mark.state ? journal : NULL);
    if (result != 1)
      break;
    work->phase = MountDirectories;
    return 0;
  case MountDirectories:
    result = blkdirstep(work->dirs);
    break;
  default:
    return -ERR_STATE;
  }
  if (result < 0 && !blk->rpc)
    work->failed = result;
  return result;
}
