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
/* Open the journal for writes on the exclusively owned device, with fresh
   state after isolating old I/O and callbacks. Even EMPTY grants future
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
