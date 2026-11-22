#ifndef _FS_BLKREAD_H
#define _FS_BLKREAD_H
#include "blk.h"
#include "access.h"
#include "blklog.h"
#include "cache.h"
#include "dirgraph.h"
#include "journal.h"
#include "load.h"
#include "walk.h"

typedef struct Blkread     Blkread;
typedef struct Blkredoload Blkredoload;
typedef struct Blkprobe    Blkprobe;
typedef struct Blkredo     Blkredo;
typedef struct Blkowners   Blkowners;
typedef struct Blkdir      Blkdir;
typedef struct Blkmount    Blkmount;
typedef struct Blkfind     Blkfind;
typedef struct Blkiter     Blkiter;
typedef struct Blklookup   Blklookup;
typedef struct Blkname     Blkname;
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
  Blkread    *read;
  Cache      *cache;
  const void *reader; /* Active directory iterator, lookup or link reader. */
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

struct Blkdir
{
  Blkowners *home;
  Dirgraph   graph;
  Dirnames   names;
  Dirdecode  decode;
  u64        offset;
  isize      failed;
  u32        inode, loaded, published;
};

/* After blkhomecheck succeeds, set home and decode on zeroed work. The
   decoder normalizes a complete header. The block walker validates names
   and records ordinary edges.
   One means the traversal and graph checks both completed; zero continues. */
isize blkdirstep(Blkdir *);

struct Blkfind
{
  Blkowners  *home;
  Dirdecode   decode;
  const char *name;
  u32         inode, generation;
  u32         namelen;
  u32         loaded, found;
  u64         offset;
  Dirrecord   match;
  isize       failed;
};
struct Blkiter
{
  Blkowners *home;
  Dirdecode  decode;
  u64        offset;
  u32        inode, generation, loaded, state;
  isize      failed;
};
/* Keep work at a stable address and hold the reader from start until EOF or drain.
   Zero continues, one returns a row and a name borrowed from reader
   scratch, two is EOF. Only a row updates the record and name outputs. */
isize blkiterstep(Blkiter *, Dirrecord *, const u8 **name);
/* Stop producing rows immediately. Zero releases the exclusive reader;
   a retryable error retains it and the original fill until the next stop. */
isize blkiterstop(Blkiter *);
struct Blklookup
{
  Blkowners      *home;
  const Dirgraph *graph;
  Dirdecode       decode;
  u64             mount;
  void           *nodes;
  const Vcred    *cred;
  isize (*mode)(void *, const Vnode *, u32 *owner, u32 *group, u32 *mode);
  isize (*acquire)(void *, const Vnode *, Vnode **);
  Blkfind find;
  Vnode   link;
  u32     linkblock, linkbytes;
  char    name[255];
};
/* Vwalkops.lookup for one stable mounted volume. Keep work at a stable
   address until completion or drain. The namespace routes
   mount identities, checks directory search permission separately and
   supplies acquire to transfer one held canonical vnode on success. */
isize blklookup(void *, const Vnode *, const char *, u32, Vnode **);
/* Vwalkops.search. cred is the request snapshot from vrequestpath; mode
   supplies current permission metadata for the exact held vnode. */
isize blksearch(void *, const Vnode *);
/* Vwalkops.parent for a stable, validated directory graph. The namespace
   owns root clamping, mount crossing and invalidation on directory mutation. */
isize blkparent(void *, const Vnode *, Vnode **);
/* Vwalkops.readlink. A decoded block-backed target is returned with a NUL
   in reader scratch, borrowed until the next operation on that reader. */
isize blkreadlink(void *, const Vnode *, const char **, u64 *);
/* Stop and drain before dropping a walk with outstanding block I/O. */
isize blklookupcancel(Blklookup *);

struct Blkname
{
  const Blklookup *view;
  Blkiter          iter;
  Vnode            child, parent;
  u32              length;
  isize            failed;
  char             name[FS_NAME_BYTES];
};
/* Fscwdops.name on a stable, validated volume. The caller pins the child
   and keeps work at a stable address. On success, name remains borrowed
   until the next operation; the returned parent is held. */
isize blkdirname(Blkname *, const Vnode *, Vnode **, const char **, u32 *);
isize blkdirnamecancel(Blkname *);

struct Blkmount
{
  Blkprobe   *probe, *after;
  Blkowners  *overlay;
  Blkdir     *dirs;
  Blklog      log;
  Cacheblock *storage;
  u8         *empty;
  isize (*layout)(const u8 *, Fslayout *, u8 *);
  isize (*mark)(const u8 *, const u8 *, Journalmark *);
  isize (*retire)(const u8 *uuid, u64 txid, u8 *);
  u32   capacity, faultbytes, faultoffset, phase, writable;
  isize failed;
  u32   stopped;
};

/* Own the attached block connection through publication. If usable geometry
   is absent, submit and drain INFO before reading the superblock. Supply separate work
   for the overlay and home scans; they may share cache and reader storage.
   For writable publication set writable=1 and supply the journal and trusted
   fault boundary even for EMPTY. A read-only mount may only replay if these
   recovery resources are separately provided. Zero means continue or wait
   on the current RPC; one means ready to call blkmountadd. */
isize blkmountstep(Blkmount *);
/* Startup driver, before serving clients. Owns the image alarm for this call;
   deadline is an absolute monotonic time. One means ready. On error the
   caller still owns the mount and must stop/drain it before releasing storage. */
isize blkmountwait(Blkmount *, u64 deadline);
/* Build the unpublished root identity from the validated directory scan.
   Destination starts zero; errors leave it unchanged. The namespace owner
   still holds the backing inode through publication and use. */
isize blkmountroot(const Blkmount *, Vnode *root);
/* Publish the ready mount once. The authorized namespace owner holds
   the volume, root and covering point through insertion. */
isize blkmountadd(Blkmount *, Vmounts *, Vnode *root, Vnode *point);
/* Stop an unpublished mount. Further steps only drain outstanding work;
   the original failure is returned once no request or snapshot is held. */
isize blkmountstop(Blkmount *, isize reason);
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
