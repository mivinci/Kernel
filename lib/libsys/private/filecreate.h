#ifndef _LIBSYS_FILECREATE_H
#define _LIBSYS_FILECREATE_H
#include "operation.h"
#include "fd.h"
typedef struct Pipecreate Pipecreate;
typedef struct Opencreate Opencreate;
struct Opencreate
{
  Opkey operation;
  Fdtag tag;
  u64   flags;
};
/* Zero-initialized continuation for a pathname OPEN from cwd. The caller
   supplies an immutable pathname snapshot including its NUL, and keeps caught
   signals excluded during each local step. The existing creator route owns
   submission, result validation, ACK and any resource ATTACH or ABORT. */
isize sysfileopenprepare(Filepool *pool, Connwork *work, Conncache *cache, Fdtable *table,
                         u32 layer, u64 owner, const u64 args[8], const void *path, u64 bytes,
                         u64 fdflags, Opencreate *open, Msg *message);
isize sysfileopenabort(Filepool *pool, Connwork *work, Conncache *cache, Fdtable *table,
                       Opencreate *open);
isize sysfileopencheck(Filepool *pool, Connwork *work, Conncache *cache, Fdtable *table,
                       Opencreate *open);
/* With caught signals blocked and the original CALL handed off, request
   cooperative cancellation once. The original RPC and fd remain owned by OPEN. */
isize sysfileopeninterrupt(Filepool *pool, Connwork *work, Conncache *cache, Fdtable *table,
                           Opencreate *open, u64 now);
/* Release just the fd reservation after confirmed token revocation or the
   original backend's proved termination and drain. */
isize sysfileopendiscard(Filepool *pool, Connwork *work, Conncache *cache, Fdtable *table,
                         Opencreate *open);
/* Consume a resolved, ACKed creator and publish its File as the reserved fd.
   A resolved error releases the reservation and returns status via business
   or a native error. A success returns the new descriptor number. */
isize sysfileopenfinish(Filepool *pool, Connwork *work, Conncache *cache, Fdtable *table,
                        Opencreate *open, isize *business);
/* Nonblocking collection of the submitted OPEN after Rctx handoff, with
   caught signals blocked. Zero publishes the ordinary outcome, one retains
   an ACKed resource OPEN, and WOULD_BLOCK retains the original operation. */
isize sysfileopencollect(Filepool *pool, Connwork *work, Conncache *cache, Fdtable *table,
                         Opencreate *open, int *fd, isize *business);
/* Transfer a submitted OPEN to background cleanup and release its unpublished
   fd reservation. An ACKed resource result may transfer before ATTACH. */
isize sysfileopenabandon(Filepool *pool, Connwork *work, Conncache *cache, Fdtable *table,
                         Opencreate *open);
/* With caught signals blocked, advance an abandoned OPEN or PIPE without
   publishing descriptors. Ordinary success enters the close ledger; a
   resource OPEN is compensated with ABORT by exact token before release. */
isize sysfilecreatordrain(Filepool *pool, Connwork *work, Conncache *cache, const Opkey *key);
struct Pipecreate
{
  Opkey operation;
  Fdtag tags[2];
};
/* Zero-initialized caller-owned continuation; keep it intact with the exact
   table until done. All three steps exclude caught signals; the caller binds
   operation to the existing creator CALL/wait route between steps. */
isize sysfilepipeprepare(Filepool *pool, Connwork *work, Conncache *cache, Fdtable *table,
                         u32 layer, u64 owner, Pipecreate *pipe, Msg *message);
isize sysfilepipeabort(Filepool *pool, Connwork *work, Conncache *cache, Fdtable *table,
                       Pipecreate *pipe);
/* Give the submitted creation to the completion worker and release fd reservations. */
isize sysfilepipeabandon(Filepool *pool, Connwork *work, Conncache *cache, Fdtable *table,
                         Pipecreate *pipe);
/* Native errors before consuming the operation preserve the continuation.
   On completion, business contains the reply status, or zero; a checked
   output fault queues both closes and returns the native copy error. Native
   queued cancellation consumes the continuation and returns ERR_CANCELLED. */
isize sysfilepipefinish(Filepool *pool, Connwork *work, Conncache *cache, Fdtable *table,
                        Pipecreate *pipe, int *userfds, isize *business);
/* Publish an already collected ordinary PIPE result. Keep signals excluded
   across the checked output and paired commit. On error, caller still owns
   both reservations and File IDs and must cancel/drop them. */
isize sysfilepipeinstall(Fdtable *table, const Fdtag tags[2], const u64 files[2], int *userfds);
/* Protected read-only validation of the exact creator/pool responsibility. */
isize sysfilecreatecheck(Filepool *pool, Connwork *work, Conncache *cache, const Opkey *key);
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
   business result or drained native queued cancellation is captured in
   error and creates no reference. Other native or protocol failure keeps
   every original responsibility; never reissues. */
isize sysfilecreateresult(Filepool *pool, Connwork *work, Conncache *cache, const Opkey *key);
/* Explicit recovery of an uncaptured creator whose original RPC fixed native
   ERR_PEER. The exact source backend must already be marked ended and actual
   RQ_FINISH must hold. Records the native error without inventing a reference;
   existing ack/settle/finish then release the prospective group and old pin.
   A captured successful/resource result stays owned and is refused here. */
isize sysfilecreatefailed(Filepool *pool, Connwork *work, Conncache *cache, const Opkey *key);
isize sysfilecreateack(Filepool *pool, Connwork *work, Conncache *cache, const Opkey *key);
/* Ordinary success only, or a known no-reference error. closing selects
   unpublished-reference compensation through existing close anchors.
   Resource OPEN keeps its stage/CALL/token for ATTACH/ABORT and refuses this
   ordinary settle path. No fd publication or remote cleanup here. */
isize sysfilecreatesettle(Filepool *pool, Connwork *work, Conncache *cache, const Opkey *key,
                          u32 closing);
/* Resource OPEN only, after original creator ACK. backend is the exact cold
   peer's initialized work page, possibly work itself. Its bootstrap must be
   free. Binds that record to the prospective File ID and original token;
   outputs a borrowed CALL and claim for the existing Rctx bootstrap path.
   No remote call or mapping occurs here. */
/* Match or reserve a directory tag using the captured backend's exact Image.
   Caller excludes caught signals and initializes an unclaimed work page. */
isize sysfilecreatebackend(Filepool *pool, Connwork *work, Conncache *cache, const Opkey *key,
                           Conntag *out);
isize sysfilecreateattachprepare(Filepool *pool, Connwork *work, Conncache *cache, const Opkey *key,
                                 Connwork *backend, u64 deadline, Connclaim *claim, Handle *call);
/* Positive ACKED ATTACH transport, before mapping/publication. Moves the
   creator CALL to the exact backend's stable work page without committing
   the prospective File or consuming MEM/RPC bootstrap ownership. Useful when
   mapping failed: token ABORT may then borrow the retained backend CALL.
   A redundant CALL free failure remains owned by Createop for retry. */
isize sysfilecreatekeepcall(Filepool *pool, Connwork *work, Conncache *cache, const Opkey *key,
                            Connwork *backend, Connclaim claim);
/* Original ATTACH must be ACKED; first MEM has an external verified map,
   reuse has an empty map. Moves CALL/MEM/map ownership into backend, commits
   the reserved reference and finishes only this ATTACH. A failed redundant
   CALL free retains that CALL in the creator for retry. No fd publication. */
isize sysfilecreateattached(Filepool *pool, Connwork *work, Conncache *cache, const Opkey *key,
                            Connwork *backend, Connclaim claim, Handle space, Bootmap *map,
                            u32 closing);
/* Token cleanup uses work's free bootstrap and no directory entry. call=0
   borrows the creator's still-owned CALL; otherwise it must cold-resolve to
   the original peer and remain externally owned through transport. This also
   handles a CALL already transferred to backend before a later local failure.
   Previous ATTACH RPC/MEM/map keeps its independent stable responsibility;
   this token cleanup does not consume or release that transport. */
isize sysfilecreaterevokeprepare(Filepool *pool, Connwork *work, Conncache *cache, const Opkey *key,
                                 Handle call, u64 deadline, Connclaim *claim,
                                 Handle *borrowed_call);
/* Only exact ACKED successful ABORT releases the reserved reference. Free of
   a still-owned creator CALL is separate and retryable; failure keeps token,
   reservation and original creator page. Does not release a backend's CALL. */
isize sysfilecreaterevoked(Filepool *pool, Connwork *work, Conncache *cache, const Opkey *key,
                           Connclaim claim);
/* After sysctxcreatepeerended proved the exact actual backend and the bound
   Rctx continuation has finished. A submitted failed ABORT must actually
   RQ_FINISH and ACK before releasing its prospective reference/CALL; a failed
   pre-submission CALL has no RPC and unprepares its bootstrap. Native drain,
   ACK or free failure preserves the remaining proof/responsibility. Cached
   connections, prior ATTACH/MEM and unrelated references are independent. */
isize sysfilecreateended(Filepool *pool, Connwork *work, Conncache *cache, const Opkey *key);
/* Exact ATTACH backend independently fixed as ended, source OPEN already
   captured/ACKed, bound Rctx finished. Match peer, token, claim and prospective
   File ID before retiring only this reserve and creator-owned CALL. Original
   ATTACH must actually FINISH/ACK; failure keeps remaining ownership. No new
   ABORT, no success/revocation fiction, and no source-VFS end mark. Leaves
   bootstrap/MEM/map/cached CALL for separate transport cleanup, including its
   original owner cookie after this File reserve has gone. A source ABORT
   still owned by this same File/token is BUSY and
   must first finish or be explicitly unprepared. */
isize sysfilecreatebackendended(Filepool *pool, Connwork *work, Conncache *cache, const Opkey *key,
                                Connwork *backend, Connclaim claim);
/* After logical revocation/termination and Rctx handoff, clean this exact
   ATTACH before releasing the creator. The caller holds signals blocked. */
isize sysfilecreateattachdiscard(Filepool *pool, Connwork *work, Conncache *cache, const Opkey *key,
                                 Connwork *backend, Connclaim claim, Handle space, Bootmap *map);
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
