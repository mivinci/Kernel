#ifndef _FS_JOURNAL_H
#define _FS_JOURNAL_H
#include "geometry.h"
#define JOURNAL_BYTES       FS_BLOCK_BYTES
#define JOURNAL_META        16
#define JOURNAL_DATA        128
#define JOURNAL_WRITE       1
#define JOURNAL_FLUSH       2
#define JOURNAL_IDLE        0
#define JOURNAL_DATA_WRITE  1
#define JOURNAL_DATA_FLUSH  2
#define JOURNAL_LOG_WRITE   3
#define JOURNAL_LOG_FLUSH   4
#define JOURNAL_MARK_WRITE  5
#define JOURNAL_MARK_FLUSH  6
#define JOURNAL_HOME_WRITE  7
#define JOURNAL_HOME_FLUSH  8
#define JOURNAL_EMPTY_WRITE 9
#define JOURNAL_EMPTY_FLUSH 10
#define JOURNAL_DONE        11
#define JOURNAL_STOPPED     12
typedef struct Journal      Journal;
typedef struct Journalblock Journalblock;
typedef struct Journalplan  Journalplan;
typedef struct Journalio    Journalio;
typedef struct Journalmark  Journalmark;
typedef struct Journalhead  Journalhead;
struct Writeupdate;
struct Writeimage;
struct Loadmap;
struct Loadindex;
struct Blockmap;
struct Cache;
struct Reclaim;
typedef struct Mapmeta     Mapmeta;
typedef struct Growth      Growth;
typedef struct Reclaimtx   Reclaimtx;
typedef struct Reclaimscan Reclaimscan;
typedef struct Inodemap    Inodemap;
typedef struct Inodealloc  Inodealloc;
typedef struct Dirinsert   Dirinsert;
typedef struct Dirgrow     Dirgrow;
typedef struct Shrink      Shrink;
struct Journalmark
{
  u64 txid;
  u32 state, headercrc, payloadcrc;
};
struct Journalhead
{
  u64 txid, targets[JOURNAL_META];
  u32 state, count, headercrc, payloadcrc;
};
struct Journalblock
{
  u64       block;
  const u8 *bytes;
};
struct Journalplan
{
  const u8           *header, *committed, *empty;
  const Journalblock *metadata, *data;
  u32                 metadata_count, data_count;
};
struct Journalio
{
  u64       serial, txid, block;
  const u8 *bytes;
  u32       kind, reserved;
};
struct Mapmeta
{
  const u8 *inode, *bitmap, *index;
  const u8 *header, *committed, *empty;
};
struct Growth
{
  Journal                  *journal;
  struct Cache             *cache;
  const struct Writeupdate *update;
  u64                       txid;
};
struct Reclaimtx
{
  Journal              *journal;
  struct Cache         *cache;
  const struct Reclaim *batch;
  struct Loadmap       *live;
  struct Loadindex     *index;
  struct Blockmap      *allocation;
  u64                   txid;
  Inodemap             *inodes;
};
struct Inodemap
{
  u64 used[FS_INODES / 64];
};
struct Inodealloc
{
  Journal        *journal;
  struct Cache   *cache;
  struct Loadmap *live;
  Inodemap       *inodes;
  u64             txid, inode, generation;
  const Dirgrow  *grow;
};
struct Dirgrow
{
  struct Loadmap   *parent;
  struct Loadindex *index;
  struct Blockmap  *allocation;
  const u8         *bitmapimage, *indeximage;
  u32               newindex;
};
struct Dirinsert
{
  u64            parent;
  Journalblock   directory;
  const u8      *parentimage;
  const Dirgrow *grow;
};
struct Shrink
{
  Journal              *journal;
  struct Cache         *cache;
  struct Loadmap       *live;
  const struct Loadmap *staged;
  u64                   txid, length;
};
struct Reclaimscan
{
  u32 next, left;
};
struct Journal
{
  u64          blocks, last, txid, serial;
  const u8    *header, *committed, *empty;
  Journalblock metadata[JOURNAL_META], data[JOURNAL_DATA];
  Journalio    pending;
  u32          metadata_count, data_count, phase, cursor;
  u32          active, durable, reserved[2];
};
STATIC_ASSERT(sizeof(Journalblock) == 16 && sizeof(Journalio) == 40, journal_io_size);
STATIC_ASSERT(sizeof(Journalplan) == 48 && sizeof(Journal) == 2432, journal_state_size);
/* Private serialized single-writer sequencing core, not a disk codec, target
   classifier, mount/recovery routine, or transport. Initialize fresh state
   only after a validated durable EMPTY(last), isolated/drained old IO and
   compatible geometry/flush guarantees. Discard old tickets/callbacks before
   initializing a fresh lifetime; serials are local to that lifetime. Never
   reinitialize live authority. */
isize journalinit(Journal *journal, u64 blocks, u64 last);
/* Trusted owner supplies already encoded/validated header and markers for
   last+1, and a complete validated metadata transaction view. Numeric target
   checks here cannot classify directory/index blocks. Descriptors are copied;
   every independent 4KiB snapshot stays immutable, owned and alive through
   release. Source storage must not overlap state, descriptors or snapshots.
   A nonempty data-only batch uses no records, ends after data FLUSH and
   leaves last unchanged. Use only for overwrites of committed data blocks. */
isize journalbegin(Journal *journal, const Journalplan *plan);
/* Start a growth or hole-allocation transaction from immutable data images
   and already encoded/validated metadata. The header's targets must follow
   inode, bitmap if allocated, then index if edited. Keep the inode guard,
   reservations and images through completion; publish only after commit. */
isize journalgrowth(Growth *, Journal *, struct Cache *, const struct Writeupdate *,
                    const struct Writeimage *, const Mapmeta *);
/* Consume the exact completed growth and retire its journal, once. All
   views are independent. Cached home blocks are invalidated before the new
   map is visible. For an allocation, prepared is the private view
   encoded in bitmap. Keep the inode guard and volume writer, and coordinate
   cache visibility before releasing them. Do not retire the journal first.
   A stopped growth is discarded only after I/O drains and recovery owns it. */
isize growthpublish(Growth *, struct Loadmap *, struct Loadindex *, struct Blockmap *,
                    const struct Blockmap *prepared);
/* Begin a metadata-only transaction for a loadreclaim batch. Encode the
   inode iff its pointers change, always encode the new bitmap, and encode
   the index iff its entries change (target order: inode, bitmap, index).
   The trusted caller holds the inode and volume writer guards, keeps all
   storage disjoint and snapshots immutable, and excludes new accesses to
   the affected blocks until retirement and publication. An orphan batch
   also requires the inode to remain unlinked and free of all references.
   On stop, drain I/O
   and discard the transaction only when recovery takes ownership. */
isize journalreclaim(Reclaimtx *, Journal *, struct Cache *, const struct Reclaim *,
                     struct Loadmap *, struct Loadindex *, struct Blockmap *, const Mapmeta *);
/* Final retirement of an unlinked and unreferenced inode with no block
   pointers. Encode only the inode bitmap (Mapmeta.bitmap); keep the old
   generation in the inode row. Hold the inode and volume writer guards
   and retain all views through publication or recovery. */
isize journalinodefree(Reclaimtx *, Journal *, struct Cache *, struct Loadmap *, Inodemap *,
                       u32 links, u32 refs, const Mapmeta *);
/* Stage an empty inode from an unallocated decoded row. Generation 0 is a
   never-used slot; UINT32_MAX is exhausted. The root is never allocated here. */
isize inodeallocstage(const Inodemap *, const struct Loadmap *, struct Loadmap *);
/* Commit the inode row and bitmap together, optionally with an insertion
   into an existing directory block. The caller holds the volume writer and
   namespace guards, validates absence and directory ownership, and supplies
   immutable encoded records. Target order: child inode table, inode bitmap,
   directory, then parent inode table if distinct. When the table is shared,
   encoded->inode contains both rows and parentimage must be NULL. This
   grow, if present, appends one full directory block at EOF. The caller
   reserves the selected directory block and optional new index block under
   the volume writer, and keeps the descriptor and live views disjoint and
   unchanged. The block bitmap and any changed index follow the parent in
   the log. Keep the name and slot unpublished and exclude readers until
   publication or recovery. */
isize journalinodealloc(Inodealloc *, Journal *, struct Cache *, Inodemap *, struct Loadmap *,
                        const struct Loadmap *, const Mapmeta *, const Dirinsert *);
isize inodeallocpublish(Inodealloc *);
/* After durable EMPTY, forget the affected cache bindings and consume the
   transaction once, updating live pointers and returning blocks to the allocator. */
isize reclaimpublish(Reclaimtx *);
/* Advance an owned batch by one I/O, or publish it after retirement.
   Return one on publication, zero while awaiting I/O or cache ownership,
   or an error. On error retain the batch for the volume owner to recover. */
isize reclaimstep(Reclaimtx *, isize (*submit)(void *, const Journalio *, u64, u32), void *);
/* Schedule a volatile sweep from the current position. The owner calls this
   at mount and after changes that can make previously skipped inodes ready. */
void reclaimscanwake(Reclaimscan *);
/* Examine up to 32 inode numbers; stop after starting one transaction.
   Candidate returns one only with a started batch, zero for no work, BUSY
   or WOULD_BLOCK to skip for this sweep, or another error to stop. */
isize reclaimscanstep(Reclaimscan *, Reclaimtx *, isize (*candidate)(void *, u32), void *);
/* Prepare the next batch from the committed bitmap and current cache state.
   Pass the kind from loadreclaimkind while holding inode mutation authority.
   Add any independent transport ownership in io. Hold the inode and volume
   writer guards through encoding and submission. As with loadreclaim, zero
   or error leaves the proposed outputs unchanged. */
isize reclaimprepare(const Journal *, struct Cache *, const struct Loadmap *,
                     const struct Loadindex *, u32 kind, const struct Blockmap *io,
                     const struct Blockmap *committed, struct Blockmap *prepared, struct Reclaim *);
/* Commit the inode image encoded from loadmapshrink. Equal lengths need
   no transaction. Keep distinct, immutable staging/record storage and hold
   the inode mutation guard and volume writer until publication or recovery.
   Exclude cache accesses to the inode home block during the transaction. */
isize journalshrink(Shrink *, Journal *, struct Cache *, struct Loadmap *, const struct Loadmap *,
                    const Mapmeta *);
isize shrinkpublish(Shrink *);
/* Open the journal for writes on the exclusively owned device. State must
   be zero initialized or STOPPED and released. Reusing released state
   preserves ticket serials. The caller must first isolate old I/O and
   callbacks. Even EMPTY grants future
   write access, so it needs the same geometry, flush and fault guarantees.
   The decoder checks magic, padding and checksums; payloadcrc covers the complete
   images. The classifier reads the whole immutable transaction view (positive
   = metadata, zero/negative = reject). For COMMITTED, empty is the encoded
   EMPTY record for this txid, and it and the images stay immutable until
   release. An EMPTY mark does not use the head, images or classifier. */
isize journalrecover(Journal *, const Fslayout *, const Fsgeometry *, const Journalmark *,
                     const Journalhead *, const Journalblock *, u32 count, u32 payloadcrc,
                     const u8 *empty, int (*classify)(void *, const Journalblock *, u32, u32),
                     void *);
/* One outstanding operation. This reserves an IO anchor but does not submit
   it. Submission failure with no lower references may complete with success=0.
   Repeated next while outstanding returns BUSY, never a second submission. */
isize journalnext(Journal *journal, Journalio *out);
/* Translate the exact outstanding ticket to a byte offset and length.
   FLUSH has zero offset and length. A stopped operation cannot be submitted. */
isize journalrange(const Journal *, const Journalio *, u64 *offset, u32 *bytes);
/* Drive one operation per event-loop turn. Return one at DONE, zero while
   waiting, or an error. Submit copies the ticket before returning; a negative
   result guarantees no lower access remains. Completion is delivered via
   journalcomplete after drain. The owner retains snapshots and publishes. */
isize journalpump(Journal *, isize (*submit)(void *, const Journalio *, u64, u32), void *);
/* Call only when all lower access for this exact ticket has actually drained.
   Write success alone is not durability; only the succeeding flush advances
   that boundary. Known failure stops all new work, never implicitly retries. */
isize journalcomplete(Journal *journal, const Journalio *ticket, u32 success);
/* Timeout/result-only/cancel: stop immediately but retain outstanding ticket
   and snapshots. Completing it later only drains; it cannot resume this run. */
isize journalstop(Journal *journal);
/* DONE advances last; STOPPED requires IO drained, leaves last unchanged and
   stays STOPPED. Only a fresh, validated recovery may permit future writes.
   durable means COMMITTED flush succeeded, not home retirement or reuse. */
isize journalrelease(Journal *journal);
#endif
