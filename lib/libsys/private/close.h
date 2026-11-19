#ifndef _LIBSYS_CLOSE_H
#define _LIBSYS_CLOSE_H
#include "fileref.h"
#include "operation.h"
#define SYSFILE_CLOSE_OPTIONS (CALL_ASYNC | CALL_CONTROL)
/* Short protected adapter, not POSIX close. A persistent last-close anchor
   already exists, old fd is detached and pins=0. Builds slot6 descriptor with
   real backend reference_id and the frozen four-word request. Caller submits
   exactly once using SYSFILE_CLOSE_OPTIONS and moves the original RPC before
   restoring signals. Failure before submission leaves anchor in Filepool. */
isize sysfilecloseprepare(Filepool *pool, Connwork *work, Conncache *cache, const Fileclose *anchor,
                          u64 owner, u64 deadline, Opkey *key, Msg *message);
/* Validates only a positive logical-close confirmation on the original Rq.
   Errors/unknown results keep anchor/op/resources. Call before SAVED/ACK. */
isize sysfilecloseresult(Filepool *pool, Connwork *work, Conncache *cache, const Fileclose *anchor,
                         const Opkey *key);
/* After successful result and native ACK, atomically returns control page
   responsibility and confirms original anchor. No second CLOSE on retry. */
isize sysfileclosecomplete(Filepool *pool, Connwork *work, Conncache *cache,
                           const Fileclose *anchor, const Opkey *key);
#endif
