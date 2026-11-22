#ifndef _FS_SYNC_H
#define _FS_SYNC_H
#include "txn.h"

typedef struct Syncwork Syncwork;
struct Syncwork
{
  Txn   *txn;
  void  *snapshots;
  u32    slots;
  Handle request;
};

/* Run the data portion of one sync on a mounted volume. The owner supplies
   the volume's Txn and holds the allocation stable until release. It also
   accounts for metadata and prior device errors before replying to the client.
   Submit copies the ticket and wakes the loop after completing it. On a
   submission error no lower access may remain. Return values match Fsreads.sync.
   Once captured, the private batch can finish even if its request is cancelled. */
isize filesyncstep(Syncwork *, Handle request, u64 state, const struct Loadmap *,
                   const struct Loadindex *, isize (*submit)(void *, Handle, const Journalio *),
                   void *context);
/* Call only after lower access to the exact ticket has ended. */
isize filesynccomplete(Syncwork *, Handle request, const Journalio *, u32 success);
#endif
