#ifndef _LIBSYS_FILECREATE_H
#define _LIBSYS_FILECREATE_H
#include "operation.h"
/* Private per-slot creator responsibility in the existing connection page.
   Caller excludes caught signals. Inputs are private accepted snapshots.
   owner is the Rctx incarnation; context is MAIN/SIGNAL1/SIGNAL2. Source is
   zero for OPEN/PIPE without a directory, or one already pinned local File;
   DUPREF requires the exact pinned source. It moves only on success.
   Reserves output references before submission and builds one descriptor.
   No CALL is made. Outputs/inputs/pool/work are independent storage. */
isize sysfilecreateprepare(Filepool *pool, Connwork *work, Conncache *cache, u32 context, u64 owner,
                           u64 opcode, u64 object, u64 *owned_source, const u64 args[8],
                           const void *payload, u64 bytes, Opkey *key, Msg *message);
/* Poll original RPC, validate frozen ordinary/resource creator shapes, prove
   RQ_FINISH and capture output once before any ACK. Resource OPEN cold-queries
   its real CALL peer and moves that CALL into Createop. A valid negative
   business result is captured in error and creates no reference. Native or
   protocol failure keeps every original responsibility; never reissues. */
isize sysfilecreateresult(Filepool *pool, Connwork *work, Conncache *cache, const Opkey *key);
isize sysfilecreateack(Filepool *pool, Connwork *work, Conncache *cache, const Opkey *key);
/* Ordinary success only, or a known no-reference error. closing selects
   unpublished-reference compensation through existing close anchors.
   Resource OPEN keeps its stage/CALL/token for ATTACH/ABORT and refuses this
   ordinary settle path. No fd publication or remote cleanup here. */
isize sysfilecreatesettle(Filepool *pool, Connwork *work, Conncache *cache, const Opkey *key,
                          u32 closing);
/* ACKED + RESOLVED only. Moves result IDs and the old source pin once; caller
   publishes/drops those IDs and returns the source pin separately. closing=1
   leaves ownership in the pool's close anchors and returns zero result IDs. */
isize sysfilecreatefinish(Filepool *pool, Connwork *work, Conncache *cache, const Opkey *key,
                          u64 files[2], u64 *source);
/* PREPARED before submission only. Restores the caller's source pin and
   releases local prospective slots. This never cancels remote creation. */
isize sysfilecreateabort(Filepool *pool, Connwork *work, Conncache *cache, const Opkey *key,
                         u64 *source);
#endif
