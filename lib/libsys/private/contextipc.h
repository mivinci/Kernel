#ifndef _LIBSYS_CONTEXTIPC_H
#define _LIBSYS_CONTEXTIPC_H
#include "context.h"
/* Derive the three Fix layers from the stable parent snapshot and admit every
   route before publishing any output. Output must be private and disjoint from
   the bank and transport records. No target identity or backend preparation. */
isize sysctxfixplan(Rbank *bank, Connpeer parent, Fixlayer out[FIX_LAYER_COUNT]);
/* Read-only full route admission before the coordinator changes any child
   tables. Requires inherited parent identity; no idempotent child shortcut.
   Keep the bank and transport records stable until local repair completes. */
isize sysctxfixwaitcheck(Rbank *bank, Connpeer parent, Connpeer child,
                         const Fixlayer layers[FIX_LAYER_COUNT]);
/* Child-local repair of pending IPC waits mixed with admitted raw layers.
   Validate every route against the still inherited parent records
   before modifying any layer. No poll/cancel/ACK or transport record mutation.
   Ordinary completed READ requires a full pre-fork private snapshot; WRITE
   requires a zero-byte snapshot as its pre-fork real-finish proof. Fixed
   Terminal native PEER/CANCELLED/REVOKED on ordinary READ/WRITE requires the
   same zero-byte real-finish proof and preserves its original native status.
   Local results survive another fork. Other completed/unsent IPC work still
   requires its typed repair path. */
isize sysctxfixwait(Rbank *bank, Connpeer parent, Connpeer child,
                    const Fixlayer layers[FIX_LAYER_COUNT]);
/* A resumed IPC wrapper checks this before using its copied transport key.
   Returns 1 for a fixed child interruption or completed native failure,
   0 for an ordinary route. On 1,
   status is saved locally and the layer becomes finishable, without handoff
   to parent transport. Active invocation must unwind before finish. */
isize sysctxforkresult(Rbank *bank, Connpeer self, Rkey key, isize *status);
/* Completed ordinary READ/WRITE with pre-fork snapshot/finish proof.
   Negative business replies require zero value/bytes and a zero-byte proof.
   Returns 1 and borrowed value-only reply, 0 for other routes. Snapshot view
   remains valid until finish. Never resumes a parent Op or repeats READ. */
isize sysctxforkreply(Rbank *bank, Connpeer self, Rkey key, Ipcmsg *out);
/* Short caller-protected registration before sysctxinvoke. The destination
   and its exact claim/key stay exclusively bound until handoff. No waiting,
   allocation, ACK, cancellation or application output pointer. CALL is always
   asynchronous; options select only native MEM/CALL receive consent.
   ABORT requires options=0 and automatically selects CALL_CONTROL. */
isize sysctxbootcall(Rbank *bank, Connpeer self, u32 layer, Bootstrap *record, Connclaim claim,
                     Handle call, u64 options, Rkey *out);
isize sysctxbootwait(Rbank *bank, Connpeer self, u32 layer, Bootstrap *record, Connclaim claim,
                     Rkey *out);
/* Resume an already prepared HELLO. The caller owns the exclusive claim,
   VA range, and the initially zero map and key until completion or discard.
   A native wait interruption keeps the same RPC; a mapped but unverified
   alias is retained for discard. Success publishes READY and ends bootstrap. */
isize sysctxhello(Rbank *bank, u32 layer, Connwork *work, Conncache *cache, Handle space,
                  u64 address, Bootmap *map, Rkey *active);
isize sysctxopcall(Rbank *bank, Connpeer self, u32 layer, Connwork *work, Conncache *cache,
                   const Opkey *operation, u64 options, Rkey *out);
isize sysctxopwait(Rbank *bank, Connpeer self, u32 layer, Connwork *work, Conncache *cache,
                   const Opkey *operation, Rkey *out);
/* Creators bind their actual prepaid pool before native submission/wait.
   Ordinary op routes reject a creator without this binding. OPEN consents
   only to CALL resource delivery; other creation operations have no consent. */
isize sysctxcreatecall(Rbank *bank, Connpeer self, u32 layer, Filepool *pool, Connwork *work,
                       Conncache *cache, const Opkey *operation, Rkey *out);
isize sysctxcreatewait(Rbank *bank, Connpeer self, u32 layer, Filepool *pool, Connwork *work,
                       Conncache *cache, const Opkey *operation, Rkey *out);
/* Register the next native action for a PIPE continuation. PREPARED can
   submit; WAITING can only wait on the already-owned RPC; a fixed result
   requires no native action (returns 1). Caller excludes caught signals. */
isize sysctxpipestep(Rbank *bank, Connpeer self, u32 layer, Filepool *pool, Connwork *work,
                     Conncache *cache, const Opkey *operation, Rkey *out);
/* Drive one PIPE action with native signal masking around registration and
   result handoff, leaving the original mask in force while waiting. Zeroed
   active belongs exclusively to this continuation. A handoff failure keeps
   its key for retry; a native interruption leaves the original Op intact. */
isize sysctxpipeio(Rbank *bank, Connpeer self, u32 layer, Filepool *pool, Connwork *work,
                   Conncache *cache, const Opkey *operation, Rkey *active);
/* Same one-action driver for pathname OPEN. A result (1) still belongs to
   the original creator and must be ACKed and resolved before fd publication. */
isize sysctxopenio(Rbank *bank, Connpeer self, u32 layer, Filepool *pool, Connwork *work,
                   Conncache *cache, const Opkey *operation, Rkey *active);
struct Pipecreate;
struct Opencreate;
struct Fdtable;
/* Drive OPEN using an established FS connection. Capture the application path
   in the existing layer scratch before reserving anything. An inaccessible path
   returns ERR_FAULT; a path without NUL in the first slot returns ERR_LIMIT.
   A continuation uses only the original request and ignores the input arguments.
   Zero means a completed ordinary result (fd is -1 on business failure).
   One transfers the ACKed resource result to the caller's ATTACH/ABORT path;
   neither output changes. On signal, collect the original result or request
   cooperative cancellation and wait to its original deadline. If that wait
   times out, keep open for later collection. Only native queued cancellation
   and SA_RESTART allow a fresh submission from the same private snapshot. */
isize sysctxopencreate(Rbank *bank, u32 layer, Filepool *pool, Connwork *work, Conncache *cache,
                       struct Fdtable *table, const char *path, u64 flags, u64 mode, u64 fdflags,
                       struct Opencreate *open, Rkey *active, int *fd, isize *business);
/* Drive the exact captured OPEN token against an initialized backend work
   page. A cold connection needs a caller-reserved VA; reuse needs no VA.
   Keep map and active across retries. Errors leave outstanding ownership
   for retry or explicit compensation. Success resolves OPEN for publication. */
isize sysctxopenattach(Rbank *bank, u32 layer, Filepool *pool, Connwork *work, Conncache *cache,
                       const Opkey *operation, Connwork *backend, Handle space, u64 address,
                       u64 deadline, Bootmap *map, Rkey *active);
/* Select the work page from the Image directory and drive the original ATTACH.
   Caller retains the VA reservation and continuation across retries. */
isize sysctxopenconnect(Rbank *bank, u32 layer, Filepool *pool, Connwork *connections,
                        Conncache *cache, const Opkey *operation, Handle space, u64 address,
                        u64 deadline, Bootmap *map, Rkey *active);
/* Complete an ACKed resource OPEN and return its newly published fd. A cold
   ATTACH uses the caller's reserved address. Keep the original continuations
   on failure for retry or explicit compensation. */
isize sysctxopeninstall(Rbank *bank, u32 layer, Filepool *pool, Connwork *connections,
                        Conncache *cache, struct Fdtable *table, struct Opencreate *open,
                        Handle space, u64 address, Bootmap *map, Rkey *active);
/* Revoke the exact OPEN token over the creator-owned CALL without a directory
   entry. Retain active across interruption. ERR_WOULD_BLOCK can mean the original
   peer ended with an accepted ABORT still draining; retry without resubmitting.
   ATTACH transport cleanup is separate. Success permits discarding the fd. */
isize sysctxopenrevoke(Rbank *bank, u32 layer, Filepool *pool, Connwork *work, Conncache *cache,
                       const Opkey *operation, u64 deadline, Rkey *active);
/* Finish any ATTACH continuation before entering. Keep the OPEN and ABORT
   continuations and the original Bootmap until cancellation succeeds. */
isize sysctxopencancel(Rbank *bank, u32 layer, Filepool *pool, Connwork *connections,
                       Conncache *cache, struct Fdtable *table, struct Opencreate *open,
                       Handle space, Bootmap *map, Rkey *active);
/* Create local descriptors over an established FS connection. Both private
   records start zeroed, and remain caller-owned across interruption or
   incomplete drain (ERR_WOULD_BLOCK after submitted peer death). Re-enter
   with the same records to continue an operation;
   the output pointer is used only during final publication, never retained. */
isize sysctxpipecreate(Rbank *bank, u32 layer, Filepool *pool, Connwork *work, Conncache *cache,
                       struct Fdtable *table, struct Pipecreate *pipe, Rkey *active, int *fds,
                       isize *business);
/* Protected nonblocking typed capture after raw handoff. Records the outcome
   in route.capture_status, preserving the native status separately. Failure
   keeps the original Op/Createop and every reservation/resource; no ACK or fd
   publication. The signal prelude attempts this for a handed-off creator
   RQWAIT before entering a handler. A deferred/invalid service result does not
   suppress the signal: raw responsibility remains stable for continuation. */
isize sysctxcreateresult(Rbank *bank, Connpeer self, Rkey key);
/* Failed original asynchronous CALL, no RPC produced: exact handed-off
   route, cached target and native ERR_PEER mark that old backend ended.
   No probe or new message is sent; the prospective Op stays PREPARED for
   explicit pre-submission rollback. Native wait errors use sysoppeerended. */
isize sysctxpeerended(Rbank *bank, Connpeer self, Rkey key);
/* Exact HELLO/ATTACH/EXEC_CLAIM route only. After native PEER handoff, match
   its submitted target, bootstrap claim and work peer before fixing ended.
   A continued wait keeps the original target; a fresh wait-only route cannot
   supply this proof. Failed CALL without an RPC rechecks the held CALL.
   Only marks this work DRAINING/ended; no FINISH, ACK or resource release. */
isize sysctxbootpeerended(Rbank *bank, Connpeer self, Rkey key, Connwork *work, Conncache *cache);
/* A detached token ABORT failed with native ERR_PEER.
   Validates the exact bound bootstrap/token/prospective ID and cold-queries
   the CALL's real PID/image before submission. A continued RQWAIT keeps that
   saved target through sysctxrewait; a fresh wait-only route has no proof.
   Failed pre-submission CALL also rechecks its held CALL. Records an
   irrevocable creator-local end proof, never marks the source VFS ended.
   No new authority, directory slot, remote request or resource release. */
isize sysctxcreatepeerended(Rbank *bank, Connpeer self, Rkey key, Filepool *pool, Connwork *work,
                            Conncache *cache, const Opkey *operation);
/* After return-window capture or normal return, under caught-signal exclusion.
   Moves raw ownership once, is idempotent after that move, and keeps DONE on
   failure. Transient wait errors keep the original RPC in its destination.
   Native status remains available to the active invocation's continuation.
   Raw sysctxresult is forbidden for a bound route. Finish remains separate. */
isize sysctxhandoff(Rbank *bank, Connpeer self, Rkey key);
/* Continue the same bound WAITING RPC after handoff, preserving incarnation,
   input and cancellation policy. Inactive only; never submits new business. */
isize sysctxrewait(Rbank *bank, Connpeer self, Rkey key);
/* Opt in only while PREPARED; raw IPC and mandatory transactions stay outside
   POSIX cancellation. Signal prelude captures/hands off before requesting
   cooperative cancel on the same foreground operation. Never waits for its
   server. Caller supplies the actual saved TF and a monotonic ns sample. */
isize sysctxinterruptible(Rbank *bank, Connpeer self, Rkey key);
isize sysctxsignal(Rbank *bank, Connpeer self, Rkey key, void *trap, u64 now);
/* After handoff, copy bounded completed output into this layer's prepaid
   snapshot. RQ_FINISH is checked nonblocking. Publication is all-or-nothing;
   repeat of the same range is idempotent, another range is rejected. Borrowed
   snapshot bytes remain valid only until sysctxfinish for this incarnation.
   No application copy, ACK, saved marker or page release is implied. */
isize sysctxsnapshot(Rbank *bank, Connpeer self, Rkey key, u64 offset, u64 bytes);
isize sysctxsnapshotview(Rbank *bank, Connpeer self, Rkey key, const void **out, u64 *bytes);
#endif
