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
typedef struct Mapmeta   Mapmeta;
typedef struct Growth    Growth;
typedef struct Reclaimtx Reclaimtx;
typedef struct Shrink    Shrink;
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
};
struct Shrink
{
  Journal              *journal;
  struct Cache         *cache;
  struct Loadmap       *live;
  const struct Loadmap *staged;
  u64                   txid, length;
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
   the affected blocks until retirement and publication. On stop, drain I/O
   and discard the transaction only when recovery takes ownership. */
isize journalreclaim(Reclaimtx *, Journal *, struct Cache *, const struct Reclaim *,
                     struct Loadmap *, struct Loadindex *, struct Blockmap *, const Mapmeta *);
/* After durable EMPTY, forget the affected cache bindings and consume the
   transaction once, updating live pointers and returning blocks to the allocator. */
isize reclaimpublish(Reclaimtx *);
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
