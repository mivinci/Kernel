#ifndef _FS_BLKREAD_H
#define _FS_BLKREAD_H
#include "blk.h"
#include "cache.h"
#include "load.h"

typedef struct Blkread     Blkread;
typedef struct Blkredoload Blkredoload;
struct Redoscan;
struct Redoinode;
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
  u32       pending, kind, links;
  u8        bytes[CACHE_BYTES];
};

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
