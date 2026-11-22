#ifndef _FS_BLKREAD_H
#define _FS_BLKREAD_H
#include "blk.h"
#include "cache.h"
#include "dirgraph.h"
#include "journal.h"
#include "load.h"

typedef struct Blkread     Blkread;
typedef struct Blkredoload Blkredoload;
typedef struct Blkprobe    Blkprobe;
typedef struct Blkredo     Blkredo;
typedef struct Blkowners   Blkowners;
struct Blkread
{
  Fsblk    *blk;
  Cachefill fill;
  isize     status;
  u32       collected;
  u8        data[CACHE_BYTES];
};

/* One zeroed instance per stable scan. Decoders are synchronous, do not retain
   the encoded input, and fully initialize outputs on validated success. */
struct Blkredoload
{
  Blkread *read;
  Cache   *cache;
  isize (*inode)(u32, const u8 *, Loadmap *, u32 *, u32 *);
  isize (*indirect)(u32, const u8 *, Loadindex *);
  Loadmap   map;
  Loadindex index;
  Dirnode  *nodes;
  u32       pending, kind, links;
  u8        bytes[CACHE_BYTES];
};

/* Read the superblock or a log block directly before a home view exists.
   After INFO, submit in the mount owner's chosen order; use fsblkpoll for
   the exact completion. The result is raw and never enters the home cache. */
isize blkreadboot(Fsblk *, u32 block);

struct Blkprobe
{
  Fsblk      *blk;
  Fslayout    layout;
  Fsgeometry  geometry;
  Journalmark mark;
  u64         generation, sequence;
  isize       failed;
  u32         phase;
  u8          uuid[16], bytes[FS_BLOCK_BYTES];
};

/* Zeroed work and exclusive use of blk after INFO. Synchronous decoders
   authenticate complete blocks and initialize all outputs. Returns one
   when layout and mark are valid, zero while reading, or an error. A
   pending poll error retains the original request for the next step. */
isize blkprobestep(Blkprobe *, isize (*layout)(const u8 *, Fslayout *, u8 *),
                   isize (*mark)(const u8 *, const u8 *, Journalmark *));

struct Blkredo
{
  const Blkprobe *probe;
  isize (*decode)(const u8 *, Journalhead *);
  u32          initial, finalxor;
  Journalhead  head;
  Journalblock images[JOURNAL_META];
  u64          sequence;
  isize        failed;
  u32          read, crc;
  u8           bytes[JOURNAL_META][FS_BLOCK_BYTES];
};

/* After a successful probe, retain exclusive device access and its geometry.
   Zero the work except probe, decoder and CRC policy. The decoder authenticates
   the complete header. One means no redo for EMPTY, or a verified COMMITTED
   payload. Only then may the owner use images, and must keep the work alive
   and unchanged through scan, replay and release. Zero means I/O pending. */
isize blkredostep(Blkredo *);

struct Blkowners
{
  Blkredo        *redo;
  const Blkprobe *home;
  Blkredoload     load;
  isize (*inodemap)(const u8 *, Inodemap *);
  isize (*blockmap)(const u8 *, u32, Blockmap *);
  Inodemap   inodes;
  Blockmap   allocation;
  Loadowners owners;
  Redoscan   scan;
  isize      failed;
  u32        phase;
};

/* After the probe, supply zeroed work, the redo state, decoders, a cold cache
   sized for the volume and its block reader. This drives log verification,
   both bitmaps and one inode-table batch per call. One means the ownership
   scan is complete. Zero means continue now or wait for blk->rpc if present. */
isize blkownersstep(Blkowners *);
/* Admit a home-only view from a fresh EMPTY probe and cold cache. After
   replay, supply its original COMMITTED probe and released journal as a
   pair. The new marker must prove retirement on the same volume. */
isize blkhomecheck(Blkowners *, const Blkprobe *before, const Journal *retired);
/* Admit a writable journal from that scan. The exclusive mount owner
   supplies the independently verified fault boundary and the encoded EMPTY
   record for this transaction. Keep snapshots stable through release. */
isize blkredoadmit(const Blkowners *, Journal *, u32 faultbytes, u32 faultoffset, const u8 *empty);

/* Fresh zeroed read state with blk set. The serialized volume owner
   reserves the connection through completion. Zero is a cache hit;
   one starts a read; a negative result starts no external access. */
isize blkreadstart(Blkread *, Cache *, u64 block, u32 metadata);
/* Read into private storage; only a full, drained success becomes visible
   in the cache. Return a native progress error while retaining ownership. */
isize blkreadpoll(Blkread *, isize *status);
/* Read one block in the stable recovery view. An overlay image wins over
   home, even if home is cached. On WOULD_BLOCK, service any pending block
   RPC with blkreadpoll before trying again. Output is private caller storage
   of CACHE_BYTES, disjoint from overlay, cache and transport storage. */
isize blkreadview(Blkread *, Cache *, const struct Redoscan *, u64 block, u32 metadata, void *out);
/* Same view, copying only the selected inode slot into FS_INODE_BYTES. */
isize blkreadinode(Blkread *, Cache *, const struct Redoscan *, u32 inode, void *out);
/* redoscanstep callback. Keep the decoded inode across an indirect read;
   even a single-entry cache can finish after evicting the inode block. */
isize blkredoload(void *, const struct Redoscan *, u32 inode, struct Redoinode *);
#endif
