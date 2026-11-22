#ifndef _FS_TXN_H
#define _FS_TXN_H
#include "cache.h"
#include "journal.h"
#define TXN_IDLE     0
#define TXN_PREPARED 1
#define TXN_RUNNING  2
#define TXN_STOPPED  3
typedef struct Txn Txn;
struct Txn
{
  Journal      journal;
  Cache       *cache;
  Cacheio      captures[CACHE_MAX];
  Journalblock metadata[JOURNAL_META], data[JOURNAL_DATA];
  u8          *snapshots;
  u32          count, metadata_count, data_count, phase;
};
STATIC_ASSERT(sizeof(Txn) == 9888, txn_state_size);
/* Serialized FS owner only. Same fresh-lifetime/recovery prerequisites as
   journalinit; one cache/device/writer, no public request supplies proofs. */
isize txninit(Txn *txn, Cache *cache, u64 last);
/* Borrow the caller's owned keys just during prepare; IO captures then hold
   exact versions independently. Metadata keys precede data in snapshots,
   which is independent prepaid (metadata_count+data_count)*4096 storage.
   Only dirty, valid, idle entries of the matching class may participate.
   With no metadata, capture only overwrites of already committed data blocks.
   No IO is submitted. Caller next encodes/validates the header and markers
   using these captured targets/bytes and journal.last+1, not current cache. */
isize txnprepare(Txn *txn, const Cachekey *metadata, u32 metadata_count, const Cachekey *data,
                 u32 data_count, void *snapshots);
/* Already validated metadata classification and encoded records required.
   Invalid records/storage leave PREPARED captures for repair or release.
   For a data-only batch all three record pointers must be null. */
isize txnstart(Txn *txn, const void *header, const void *committed, const void *empty);
isize txnnext(Txn *txn, Journalio *out);
isize txncomplete(Txn *txn, const Journalio *ticket, u32 success);
isize txnstop(Txn *txn);
/* PREPARED abort has no disk IO and keeps dirty. RUNNING may release only
   after DONE or STOPPED with all IO drained. Only DONE cleans exact captured
   versions; newer edits stay dirty. Failed release remains stopped until
   actual recovery, and no capture is consumed twice on a cleanup retry. */
isize txnrelease(Txn *txn);
#endif
