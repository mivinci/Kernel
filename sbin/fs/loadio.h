#ifndef _FS_LOADIO_H
#define _FS_LOADIO_H
#include "load.h"
#include "private/file.h"
#include <sys/channel.h>

typedef struct Loadio Loadio;
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
#endif
