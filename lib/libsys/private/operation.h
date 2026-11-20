#ifndef _LIBSYS_OPERATION_H
#define _LIBSYS_OPERATION_H
#include "connection.h"
#define SYSOP_FREE           0
#define SYSOP_PREPARED       1
#define SYSOP_WAITING        2
#define SYSOP_RESULT         3
#define SYSOP_ACKED          4
#define SYSOP_BACKGROUND     1
#define SYSOP_SAVED          2
#define SYSOP_RESOURCE_MOVED 4
#define SYSOP_BAD_RESULT     8
#define SYSOP_CANCEL_SENT    16
#define SYSOP_APP_ENDED      32
#define SYSOP_CREATOR        64
#define SYSOP_ABANDONED      1
#define SYSOP_CONTROL_NS     UINT64_C(1000000000)
typedef struct Opkey Opkey;
struct Opkey
{
  Conntag   connection;
  Filelease lease;
  u64       owner;
};
STATIC_ASSERT(sizeof(Opkey) == 40, slot_operation_key_size);
/* Caller excludes caught signals for short operations; no blocking calls.
   Read-only keys stay stable for the call and must not overlap any output.
   File is a single already-pinned local reference, not a service authority.
   prepare moves it into the starting slot with the full page span. Caller
   builds/validates the actual shared descriptor/payload and submits its Rq
   before moving the returned RPC. Typed creators instead hold a reserved
   carrier with SYSOP_CREATOR; generic abort/finish cannot return it as a pin.
   No app pointer or restart lives here. */
isize sysopprepare(Connwork *work, Conncache *cache, u32 context, u32 span, u64 owner,
                   u64 *owned_file, u64 object, u64 opcode, u64 deadline, Opkey *out);
isize sysopsubmitted(Connwork *work, Conncache *cache, const Opkey *key, Handle *owned_rpc);
/* Borrow PREPARED short request or WAITING RPC, never another owner. */
isize sysoprequest(Connwork *work, Conncache *cache, const Opkey *key, Msg *out);
isize sysoprpc(Connwork *work, Conncache *cache, const Opkey *key, Handle *out);
/* Exact private key/state check; no poll or ownership change. */
isize sysopcheck(Connwork *work, Conncache *cache, const Opkey *key);
/* Original bound RPC must currently return native ERR_PEER and an empty
   tuple. This fixes the old backend Image as ended and invalidates only its
   exact directory entry. Does not ACK, drain or release any page/reference;
   REVOKED/CANCELLED/timeouts are not peer-death proof. */
isize sysoppeerended(Connwork *work, Conncache *cache, const Opkey *key);
/* Nonblocking real-finish proof for a fixed result, or already ACKED. */
isize sysopfinished(Connwork *work, Conncache *cache, const Opkey *key);
isize sysopcapture(Connwork *work, Conncache *cache, const Opkey *key, isize status,
                   const Ipcmsg *result);
isize sysoppoll(Connwork *work, Conncache *cache, const Opkey *key);
isize sysopcancel(Connwork *work, Conncache *cache, const Opkey *key);
/* Explicit foreground POSIX policy only: READ/WRITE/OPEN/POLL. Fresh policy
   requires deadline=0; first successful cancel stores now+CONTROL_NS, never
   resets it. Completed results win and background/transactional work refuses
   this policy. now is the runtime's monotonic nanosecond sample. */
isize sysopinterruptcheck(Connwork *work, Conncache *cache, const Opkey *key);
isize sysopinterrupt(Connwork *work, Conncache *cache, const Opkey *key, u64 now);
/* After the handler and Rctx handoff/finish, while signals are excluded:
   poll the original RPC first. 0 keeps its fixed result, WOULD_BLOCK keeps
   waiting, ABANDONED uniquely moves an expired cancelled operation into out
   and clears owned_key. The frontend maps ABANDONED to EIO, never EINTR or
   zero progress; this native helper does not write errno or app memory. */
isize sysopresume(Connwork *work, Conncache *cache, Opkey *owned_key, u64 now, Opkey *out);
/* Explicitly abandoned READ/WRITE only. Nonblocking late-result/real-finish
   collection, drop any installed MEM/CALL reply, then ACK and return the
   pinned File once. file must initially be zero. Never copies payload or
   compensates OPEN/PIPE/reference-creating transactions by guessing. */
isize sysopdrain(Connwork *work, Conncache *cache, const Opkey *key, u64 *file);
/* Exact old backend independently ended/DRAINING, bound Rctx finished.
   READ/WRITE only, never a creator or CLOSE. Unsubmitted work can abort;
   submitted work must retain an empty native PEER result and actually FINISH
   before SAVED/ACK/page release. A fixed success or different error stays
   owned for its original result path, not rewritten or replayed. Foreground
   or background keys allowed; file starts zero and receives the still-pinned
   File once, for explicit unpin. No application copy, errno or alias close. */
isize sysoprecover(Connwork *work, Conncache *cache, const Opkey *key, u64 *file);
/* Known successful ordinary result, or the frozen 8-byte reference output
   of resource OPEN. Resource OPEN permits only offset=0 and at most 8 bytes;
   its token register is not a byte count. The resource stays owned here.
   Nonblocking RQ_FINISH confirms real
   access ended, then copy a privately bounded part of the held span. Caller
   validates business counts/descriptor semantics. No ACK/SAVED/release. */
isize sysopcopy(Connwork *work, Conncache *cache, const Opkey *key, u64 offset, u64 bytes,
                void *out);
/* Rctx-to-background unique move. Returns same connection/lease, owner=0,
   clears input key and makes old foreground keys stale. */
isize sysopbackground(Connwork *work, Conncache *cache, Opkey *owned_key, Opkey *out);
/* Explicit application-ended move; does not imply result/remote compensation. */
isize sysopabandon(Connwork *work, Conncache *cache, Opkey *owned_key, Opkey *out);
/* Caller saved result/data/resource ownership and confirmed access ended.
   Shared DONE is insufficient. ACK still checks native server_finished. */
isize sysopsaved(Connwork *work, Conncache *cache, const Opkey *key);
isize sysopack(Connwork *work, Conncache *cache, const Opkey *key);
isize sysopresource(Connwork *work, Conncache *cache, const Opkey *key, Handle *out);
/* Finish returns pinned File and releases pages only after saved+ACKED and
   resource transfer. Abort only PREPARED, before any original submission. */
isize sysopfinish(Connwork *work, Conncache *cache, const Opkey *key, u64 *file);
isize sysopabort(Connwork *work, Conncache *cache, const Opkey *key, u64 *file);
#endif
