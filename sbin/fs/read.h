#ifndef _FS_READ_H
#define _FS_READ_H
#include "cache.h"
#include "load.h"
#include "seek.h"

typedef struct Readwork  Readwork;
typedef struct Readfetch Readfetch;
struct Readfetch
{
  Cacheio ticket;
  u32     block;
};
struct Readwork
{
  Cachespan span;
  Cachefill fill;
  Handle    request;
  u32       failed;
};

/* The serialized dispatcher has authorized a readable regular File and
   admitted this request. It keeps the map, cursor and descriptor stable
   across retries. A zeroed work record pins the data blocks until reply.
   Return 1 with a read ticket to submit, or WOULD_BLOCK to await progress.
   Scratch is private independent storage of CACHE_BYTES. */
isize fileread(Readwork *work, Readfetch *fetch, Fileinflight *active, const Ipcmsg *call,
               const Fileslot *slot, const Loadmap *map, const Loadindex *index, Cache *cache,
               u32 first, u64 *position, void *scratch, volatile u8 *page);
/* After transport drains: a full independent block, or NULL/0 for failure. */
isize filereadcomplete(Readwork *work, const Cacheio *ticket, const void *block, u32 bytes);
#endif
