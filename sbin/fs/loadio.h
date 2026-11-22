#ifndef _FS_LOADIO_H
#define _FS_LOADIO_H
#include "load.h"
#include "cache.h"
#include "private/file.h"
#include <sys/channel.h>

typedef struct Loadio    Loadio;
typedef struct Loadcache Loadcache;
typedef struct Loadfetch Loadfetch;
struct Loadio
{
  Loadfile      file;
  Loadtable    *table;
  Fileinflight *active;
  volatile u8  *page;
  Handle        request;
  u64           session, ticket, slot, offset, bytes, dataoff;
  Msg           reply;
  u32           staged;
};
struct Loadfetch
{
  Cacheio ticket;
  u32     block;
};
struct Loadcache
{
  Cachespan span;
  Cachefill fill;
  u32       blocks[2], next, failed;
  Handle    request;
};

/* The dispatcher has validated the exact source connection, admitted the
   original request and captured its descriptor using sysfileaccept. Page is
   the pinned data page for that slot. On error the dispatcher still owns the
   ledger entry and must complete it. An idle work record is zero-initialized. */
isize loadiostart(Loadio *io, Loadtable *table, Fileinflight *active, const Ipcmsg *call,
                  const Fileslot *slot, volatile u8 *page, u64 now);
/* After all backing accesses drain, supply the independently staged data and
   actual bytes read. A short/failed read must not publish any data. On native
   completion failure retry the same work record, never start another read.
   After staging, retry needs no input buffer and rechecks cancellation and
   expiry before offering the result again. */
isize loadioreply(Loadio *io, const void *data, u64 bytes, u64 now);
/* Bind a resolved span to the admitted LOAD. Each pump returns 1 for a
   device read, WOULD_BLOCK while waiting, or the original reply status.
   Scratch is private preallocated storage for io->bytes. The dispatcher
   calls complete only after the exact device access has drained. */
isize loadcachebegin(Loadcache *work, Loadio *io, Cache *cache, const Loadspan *span);
isize loadcachepump(Loadcache *work, Loadio *io, Loadfetch *out, void *scratch, u64 now);
/* Full independent block on success, NULL/0 on failure. */
isize loadcachecomplete(Loadcache *work, Handle request, const Cacheio *ticket, const void *block,
                        u32 bytes);
/* Only after draining a definite non-submission. Keep the span and block
   cursor; the next pump rechecks cancellation and the LOAD deadline. */
isize loadcacheretry(Loadcache *, Handle, const Cacheio *);
#endif
