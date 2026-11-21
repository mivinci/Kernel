#ifndef _FS_CACHE_H
#define _FS_CACHE_H
#include <types.h>
#include <abi/base.h>
#define CACHE_BYTES   4096
#define CACHE_MAX     128
#define CACHE_DATA    0
#define CACHE_META    1
#define CACHE_READ    1
#define CACHE_WRITE   2
#define CACHE_CAPTURE 3
typedef struct Cache      Cache;
typedef struct Cacheblock Cacheblock;
typedef struct Cachekey   Cachekey;
typedef struct Cacheio    Cacheio;
struct Cachekey
{
  u64 generation;
  u32 index, reserved;
};
struct Cacheio
{
  Cachekey key;
  u64      serial, version;
  u32      kind, reserved;
};
struct Cacheblock
{
  u64 generation, block, version, authorized, serial, io_version;
  u32 references, valid, dirty, metadata, io, bound;
  u8  data[CACHE_BYTES];
};
struct Cache
{
  Cacheblock *entries;
  u64         blocks;
  u32         capacity, cursor;
};
STATIC_ASSERT(sizeof(Cachekey) == 16 && sizeof(Cacheio) == 40, cache_ticket_size);
STATIC_ASSERT(sizeof(Cacheblock) == CACHE_BYTES + 72 && sizeof(Cache) == 24, cache_storage_size);
/* Private serialized FS event-loop core. One fixed device instance per cache;
   fresh caller-prepaid storage only. No allocation, IO submission or native
   authority. Keys/pointers must stay owned and stable until explicitly put.
   A returned const view borrows that owned key; it is not a second copy. */
isize cacheinit(Cache *cache, Cacheblock *storage, u32 count, u64 blocks);
isize cacheget(Cache *cache, u64 block, u32 metadata, Cachekey *out);
isize cacheput(Cache *cache, Cachekey *owned);
isize cacheinvalidate(Cache *cache, Cachekey *owned);
isize cacheview(Cache *cache, const Cachekey *key, const u8 **out, u64 *version);
/* Snapshot at most one page of file data, possibly spanning two blocks.
   The caller resolves blocks from the pinned file version; zero is a hole.
   A miss leaves the independent output buffer unchanged. */
isize cachecopydata(Cache *cache, u64 first, u64 next, u32 offset, void *out, u32 bytes);
isize cacheedit(Cache *cache, const Cachekey *key, u32 offset, const void *data, u32 bytes);
/* IO tickets are separate eviction anchors. Caller may put its key while IO
   remains. Read destination is private until successful completion. Write
   snapshot/ticket outputs are independent stable preallocated storage, kept
   until actual lower-layer access drains; edits may then advance authority. */
isize cachereadbegin(Cache *cache, const Cachekey *key, Cacheio *out, u8 **destination);
isize cachewritebegin(Cache *cache, const Cachekey *key, void *snapshot, Cacheio *out);
/* success means a known finished IO, never cancellation/result-only/timeout.
   Unknown IO keeps its ticket active. This does not prove flush or DMA stop. */
isize cachecomplete(Cache *cache, const Cacheio *ticket, u32 success);
/* Metadata home write gate: trusted journal owner calls only after durable
   COMMITTED for this exact version. No public request may supply this proof.
   Does not itself write/flush/retire a transaction or authorize another edit. */
isize cacheauthorize(Cache *cache, const Cachekey *key, u64 version);
/* Trusted journal owner only: capture an immutable dirty version and hold an
   IO anchor through the whole transaction. This grants no home-write right.
   Generic cachecomplete rejects capture tickets. Finish success only after
   the journal's EMPTY flush; failure keeps dirty, and either finish requires
   all lower access to the snapshot drained. Consumes the capture ticket. */
isize cachecapture(Cache *cache, const Cachekey *key, void *snapshot, Cacheio *out);
isize cachecapturefinish(Cache *cache, Cacheio *owned, u32 success);
#endif
