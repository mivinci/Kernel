#ifndef _FS_LOAD_H
#define _FS_LOAD_H
#include <types.h>
#include <abi/base.h>
#include "geometry.h"

#define LOAD_SLOTS     32
#define LOAD_DIRECT    12
#define LOAD_INDIRECT  1024
#define LOAD_RECLAIM   32
#define RECLAIM_TAIL   1
#define RECLAIM_ORPHAN 2

typedef struct Loadfile    Loadfile;
typedef struct Loadslot    Loadslot;
typedef struct Loadtable   Loadtable;
typedef struct Loadmap     Loadmap;
typedef struct Loadindex   Loadindex;
typedef struct Loadspan    Loadspan;
typedef struct Writeplan   Writeplan;
typedef struct Writeimage  Writeimage;
typedef struct Writeupdate Writeupdate;
typedef struct Blockmap    Blockmap;
typedef struct Loadowners  Loadowners;
typedef struct Reclaim     Reclaim;
struct Loadfile
{
  u64 inode, generation, version, length;
};
/* Decoded filesystem state, not an on-disk layout. Keep both snapshots
   pinned to the same file version while resolving and reading a span. */
struct Loadmap
{
  Loadfile file;
  u32      direct[LOAD_DIRECT], indirect;
};
struct Loadindex
{
  u32 block, entries[LOAD_INDIRECT];
};
struct Loadspan
{
  u32 first, next, offset;
};
/* Resolve a LOAD range of at most one page. A missing required indirect
   snapshot returns WOULD_BLOCK; bad block references return STATE. */
isize loadmapspan(const Loadfile *file, const Loadmap *map, const Loadindex *index, u32 data_start,
                  u32 blocks, u64 offset, u64 bytes, Loadspan *out);
/* Stage a shorter length while holding the inode mutation guard. Publish
   only after committing the inode. All pointers remain for later reclaim. */
isize loadmapshrink(const Loadmap *map, u64 length, Loadmap *out);

struct Writeplan
{
  u64 length;
  u32 logical, block, offset, bytes;
  u32 tail, zerooff, zerobytes, newindex;
};
/* Plan one short write against the pinned decoded map. Allocate and zero a
   missing target before overlaying input; newindex requires a fresh indirect
   block as well. The tail range zeros part of the existing last block;
   intervening unmapped blocks remain holes. Settle truncate cleanup before
   extending and hold inode mutation authority from EOF selection to commit. */
isize loadmapwrite(const Loadmap *, const Loadindex *, u32 data_start, u32 blocks, u64 offset,
                   u64 bytes, Writeplan *);

struct Writeimage
{
  u32 block, tail;
  u8  data[FS_BLOCK_BYTES], taildata[FS_BLOCK_BYTES];
};
/* Prepare independent full data blocks in private storage. The caller still
   holds the map and mutation guard, and supplies the reserved target block
   for a hole plus the pinned old images for any existing target or distinct
   tail. A shared target/tail is written as one image. No cache or map edit. */
isize loadwritestage(const Writeplan *, u32 data_start, u32 blocks, u32 reserved,
                     const void *olddata, const void *oldtail, const void *input, Writeimage *);

struct Writeupdate
{
  Writeplan plan;
  Loadmap   map;
  Loadindex index;
  u32       indexdirty;
};
/* Build a private metadata view for one write. Both reserved blocks, when
   required, belong to the same allocation transaction. Hold the inode
   guard and settle truncate cleanup before extending. Only an index with
   indexdirty set is part of the update. This does not publish or allocate. */
isize loadwriteupdate(const Loadmap *, const Loadindex *, u32 data_start, u32 blocks, u64 offset,
                      u64 bytes, u32 data, u32 indirect, Writeupdate *);

/* Decoded transaction-local allocation view. Native words, not disk bytes. */
struct Blockmap
{
  u64 used[FS_MAX_BLOCKS / 64];
};
struct Loadowners
{
  Blockmap owned, metadata;
};
/* The single volume writer supplies its private copy of the committed map,
   with earlier reservations in the same transaction already marked. On
   success both reservations and the proposed inode update are produced;
   on failure both outputs stay unchanged. Publish only after commit. */
isize loadwritealloc(const Loadmap *, const Loadindex *, u32 data_start, u32 blocks, u64 offset,
                     u64 bytes, Blockmap *, Writeupdate *);

struct Reclaim
{
  Loadmap   map;
  Loadindex index;
  u64       blocks[LOAD_RECLAIM];
  u32       count, indexdirty, orphan;
};
/* Select one batch from a committed shorter inode. The caller holds the
   inode and volume writer guards and supplies a snapshot of blocks still
   owned by cache or I/O. Use a fresh private allocation view per batch;
   the cleared bits must only become allocatable after journal retirement.
   Return one for a prepared batch, zero for no tail to reclaim, or BUSY
   if all remaining work is pinned. Nothing changes on zero or error.
   An indirect block is removed only when its committed index is empty. */
isize loadreclaim(const Loadmap *, const Loadindex *, u32 data_start, u32 blocks,
                  const Blockmap *busy, Blockmap *allocation, Reclaim *);
/* Same batching, but all blocks are eligible regardless of EOF. Use only
   after loadreclaimkind returns ORPHAN and while new references are barred. */
isize loadorphan(const Loadmap *, const Loadindex *, u32 data_start, u32 blocks,
                 const Blockmap *busy, Blockmap *allocation, Reclaim *);
/* Classify an inode from the committed inode bitmap and decoded metadata.
   refs counts all live file and pending operation holds for orphan purposes.
   ORPHAN requires separate final inode retirement after its blocks drain. */
isize loadreclaimkind(u32 inode, u32 allocated, const Loadmap *, u32 links, u32 refs);
/* Check the decoded root from the recovered, stable view. Kind is a VNODE_*
   value. Verify root identity/linkage and local block ownership, including
   pointers beyond EOF awaiting reclaim. WOULD_BLOCK needs the matching index.
   The caller still validates directory records and cross-inode ownership. */
isize loadrootcheck(u32 allocated, u32 kind, u32 links, const Loadmap *, const Loadindex *,
                    const Blockmap *, u32 blocks);
/* Accumulate one allocated inode from a stable volume scan. Start with
   separate zeroed owners storage and visit each allocated slot exactly
   once, including orphans and their unreclaimed tails. Indirect index
   blocks and in-range directory data are metadata. Reject aliases with
   prior owners. On error or WOULD_BLOCK neither bitmap changes. */
isize loadmapclaim(u32 inode, u32 kind, u32 links, const Loadmap *, const Loadindex *,
                   const Blockmap *, u32 blocks, Loadowners *);
struct Loadslot
{
  Loadfile file;
  u64      id, image, deadline, read;
  Handle   pid;
  isize    stopped;
};
struct Loadtable
{
  Loadslot slots[LOAD_SLOTS];
  u64      serial;
};

/* Zero-initialized per FS image, serialized by the FS owner. Pass only a
   resolved, authorized file and the native source identity. The resolver and
   the mutation/reclaim paths must use the same table and inode namespace. */
isize loadopen(Loadtable *table, const Loadfile *file, Handle pid, u64 image, u64 now, u64 deadline,
               u64 *session);
/* One read in flight per session. Finish after all backing accesses drain,
   before publishing staged output; a negative result forbids publication. */
isize loadread(Loadtable *table, u64 session, Handle pid, u64 image, const Loadfile *file,
               u64 offset, u64 bytes, u64 now, u64 *ticket);
isize loadstatus(Loadtable *table, u64 session, u64 ticket, u64 now);
isize loadfinish(Loadtable *table, u64 session, u64 ticket, u64 now);
isize loadclose(Loadtable *table, u64 session, Handle pid, u64 image);
void  loadsweep(Loadtable *table, u64 now);
u64   loaddeadline(const Loadtable *table);
void  loadretire(Loadtable *table, Handle pid, u64 image);
/* Hold across content changes or inode reclamation, including deferred IO;
   renaming a path without changing or reclaiming its inode needs no guard. */
isize loadwrite(Loadtable *table, u64 inode, u64 *guard);
isize loadunwrite(Loadtable *table, u64 guard);
#endif
