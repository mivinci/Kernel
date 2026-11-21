#ifndef _LIBSYS_CLOSE_H
#define _LIBSYS_CLOSE_H
#include "fileref.h"
#include "operation.h"
#define SYSFILE_CLOSE_OPTIONS   (CALL_ASYNC | CALL_CONTROL)
#define SYSFILE_CLOSE_FAULT     1
#define SYSFILE_CLOSE_SIGNAL    2
#define SYSFILE_CLOSE_CONFIRMED 3
/* Short protected adapter, not POSIX close. A persistent last-close anchor
   already exists, old fd is detached and pins=0. Builds slot6 descriptor with
   real backend reference_id and the frozen four-word request. Caller submits
   exactly once using SYSFILE_CLOSE_OPTIONS and moves the original RPC before
   restoring signals. Failure before submission leaves anchor in Filepool. */
isize sysfilecloseprepare(Filepool *pool, Connwork *work, Conncache *cache, const Fileclose *anchor,
                          u64 owner, u64 deadline, Opkey *key, Msg *message);
/* Same signal exclusion as prepare. Claim and prepare one pending File on
   this connection; an ordinary pre-submission failure releases the claim.
   Outputs are separate, unowned storage and change only on success. */
isize sysfileclosepick(Filepool *pool, Connwork *work, Conncache *cache, u64 owner, u64 deadline,
                       Fileclose *anchor, Opkey *key, Msg *message);
/* Validates only a positive logical-close confirmation on the original Rq.
   Errors/unknown results keep anchor/op/resources. Call before SAVED/ACK. */
isize sysfilecloseresult(Filepool *pool, Connwork *work, Conncache *cache, const Fileclose *anchor,
                         const Opkey *key);
/* After successful result and native ACK, atomically returns control page
   responsibility and confirms original anchor. No second CLOSE on retry. */
isize sysfileclosecomplete(Filepool *pool, Connwork *work, Conncache *cache,
                           const Fileclose *anchor, const Opkey *key);
/* Post-handler, after Rctx handoff/finish, under caught-signal exclusion.
   Fixed confirmation is processed first. watch uses the original nonzero
   absolute control deadline supplied at prepare; it never renews it.
   FAULT uniquely moves unknown responsibility to out (future EIO), SIGNAL
   does so on explicit interruption (future EINPROGRESS). CONFIRMED preserves
   logical success while background transport cleanup remains (future 0).
   No cancel, new
   CLOSE, errno or fd-table mutation. 0 consumes owned_key after confirmation;
   WOULD_BLOCK leaves it owned. out must be separate unowned storage. */
isize sysfileclosewatch(Filepool *pool, Connwork *work, Conncache *cache, const Fileclose *anchor,
                        Opkey *owned_key, u64 now, Opkey *out);
isize sysfilecloseinterrupt(Filepool *pool, Connwork *work, Conncache *cache,
                            const Fileclose *anchor, Opkey *owned_key, Opkey *out);
/* Original background CLOSE only. Unknown/error results keep anchor and all
   remaining responsibility for lifecycle recovery; no duplicate submission. */
isize sysfileclosework(Filepool *pool, Connwork *work, Conncache *cache, const Fileclose *anchor,
                       const Opkey *key);
/* Exact old backend has independently been marked ended. These perform local
   recovery, never claim a successful service CLOSE. Caller has finished bound
   Rctx handoff/continuations; last fd alias is detached, pins=0. recover keeps
   an original CLOSE until real RQ_FINISH, native resource disposal and ACK.
   ended is for an anchor with no submitted Op; any matching slot is BUSY.
   Neither proof reclaims live aliases, pending IO, other Images or DMA. */
isize sysfilecloserecover(Filepool *pool, Connwork *work, Conncache *cache, const Fileclose *anchor,
                          const Opkey *key);
isize sysfilecloseended(Filepool *pool, Connwork *work, Conncache *cache, const Fileclose *anchor);
#endif
