#ifndef _LIBSYS_CONNECTION_H
#define _LIBSYS_CONNECTION_H
#include "bootstrap.h"
#include "file.h"
#include "fileref.h"
#define SYSFILE_CREATE_PREPARED   1
#define SYSFILE_CREATE_CAPTURED   2
#define SYSFILE_CREATE_RESOLVED   3
#define SYSFILE_RESULT_REVOKED    2
#define SYSFILE_RESULT_ENDED      3
#define SYSFILE_CREATE_PEER_ENDED 1
typedef struct Connstate Connstate;
typedef struct Connwork  Connwork;
typedef struct Slotop    Slotop;
typedef struct Createop  Createop;
struct Createop
{
  Filestage stage;
  Filevalue values[2];
  u64       files[2];
  Connpeer  peer;
  Handle    call;
  u64       token, source;
  isize     error;
  u32       phase, count, disposition, flags;
};
STATIC_ASSERT(sizeof(Createop) == 184, creator_responsibility_size);
struct Slotop
{
  Filelease lease;
  Handle    rpc;
  u64       owner, file, object, deadline;
  isize     status;
  u64       words[4];
  Handle    resource;
  u64       opcode;
  u32       phase, flags;
  u64       kind;
};
STATIC_ASSERT(sizeof(Slotop) == 128, slot_responsibility_size);
struct Connstate
{
  Connpeer  self, peer;
  Conntag   tag;
  Handle    space, memory, call;
  Bootmap   map;
  Filepages pages;
  Bootstrap bootstrap;
  Slotop    operations[FS_CONN_SLOTS];
  Createop  creators[FS_CONN_SLOTS];
  u64       ended, fork_token;
};
struct Connwork
{
  Connstate state;
  u8        reserved[PAGE_SIZE - sizeof(Connstate)];
};
STATIC_ASSERT(sizeof(Connstate) < PAGE_SIZE, connection_slot_record_budget);
STATIC_ASSERT(sizeof(Connstate) + sizeof(Conncache) < PAGE_SIZE,
              connection_directory_inside_budget);
STATIC_ASSERT(offsetof(Connwork, reserved) % 8 == 0, connection_embedded_directory_alignment);
STATIC_ASSERT(sizeof(Connwork) == PAGE_SIZE, connection_private_page);
/* One preallocated private page per backend, never shared. Fresh empty storage
   only. Slot operations carry opaque caller-pinned File references; the File
   registry and Rctx return windows remain outside this layer.
   First page reserved space can hold Conncache;
   workinit clears only Connstate, preserving the embedded directory. Caller excludes caught
   signals, owns valid separate storage and has already resolved the peer. */
isize sysconnworkinit(Connwork *work, Conncache *cache, Conntag tag);
/* Child repair only, before publishing new directory/work ownership. Caller
   supplies the child's validated SELF_SPACE and excludes caught signals.
   Removes only the inherited alias; never free/ACK a parent Handle. */
isize sysconnforkunmap(Connwork *work, Connpeer parent, Connpeer child, Handle childspace);
/* After all inherited results have been fixed in Rctx/File and aliases removed,
   discard child copies of parent transport obligations without native cleanup.
   Directory already rebound, tag selects the same backend in UNCLAIMED.
   Keep the exact child prepare token; repeated repair must match it. */
isize sysconnworkfork(Connwork *work, Conncache *cache, Connpeer parent, Conntag tag, u64 token);
/* Lazy child ATTACH only, after inheritable CALL adoption. Prepare one owned
   transport record before submission; outputs borrow the CALL and claim.
   No blocking or native submission; caller excludes caught signals. */
isize sysconnforkprepare(Connwork *work, Conncache *cache, u64 owner, u64 deadline,
                         Connclaim *claim, Handle *call);
/* Cold native lookup first. First distinct owned CALL moves into the work
   page; another Handle to the exact same peer moves to initially-zero redundant
   for explicit caller release. Same Handle is not two owned references.
   Failure preserves work/input/output. Inputs remain stable and signals are
   excluded across short QUERY calls. No backend liveness proof is implied. */
isize sysconnworktakecall(Connwork *work, Conncache *cache, Handle *owned_call, Handle *redundant);
/* Moves the installed MEM and verified alias into work while publishing READY
   under one caller-protected local commit. Own bootstrap RESULT/ACKED only.
   On failure no ownership moves. On success external map is cleared and
   bootstrap keeps its RPC/result until caller ACKs and finishes it. */
isize sysconnworkadopt(Connwork *work, Conncache *cache, Connclaim claim, Handle space,
                       Bootmap *map);
/* Ordinary ATTACH/EXEC_CLAIM confirmation only, matching existing private
   MEM/VA/connid. Resource replies cannot replace a live mapping. */
isize sysconnworkreuse(Connwork *work, Conncache *cache, Connclaim claim);
/* End one failed/abandoned ATTACH or EXEC_CLAIM on an existing READY mapping.
   Caller first settles only this prospective reference/token and its Rctx;
   existing Files/slot operations remain owned and usable. Exact original
   RQ_FINISH/ACK precedes freeing any unexpected reply-owned resource. No
   unmap, CALL release, peer-end proof or new request. Unknown/failure keeps
   the bootstrap owner; success clears only this attempt, not its old connid.
   Native PEER preserves the entire record for backend-wide death handling;
   an ordinary negative business word is not that proof.
   Caller excludes caught signals. This does not validate a successful reply
   or authorize publishing the failed reference. */
isize sysconnworkreuseabort(Connwork *work, Conncache *cache, Connclaim claim);
/* Existing mapping, exact old backend independently marked ended. Finish
   only its held bootstrap after caller's logical/Rctx settlement; unknown
   original IO retains owner. Drains/ACKs and drops reply-owned extra resource,
   never unmaps or releases old MEM/CALL/other File/slot responsibility. Keeps
   DRAINING; ordinary workclean remains gated on all remaining references. */
isize sysconnworkbootended(Connwork *work, Conncache *cache, Connclaim claim);
isize sysconnworkreserve(Connwork *work, Conncache *cache, u32 context, u32 span, Filelease *out);
isize sysconnworkrelease(Connwork *work, Conncache *cache, Filelease expected);
/* Abandon one never-published CONNECTING transport after settling logical
   token/session references and finishing bound Rctx ownership elsewhere.
   Exact claim only; invalidates locally to DRAINING, never marks peer ended.
   WAITING/unfinished RPC keeps every responsibility. Once RQ_FINISH and ACK
   succeed, unmap the still-external Bootmap and free the bootstrap-owned MEM,
   then clear only this bootstrap/owner. Retries preserve remaining steps.
   No CALL release, File rollback, remote request, READY eviction or reconnect.
   The live peer key stays DRAINING even after successful cleanup. */
isize sysconnworkdiscard(Connwork *work, Conncache *cache, Connclaim claim, Handle space,
                         Bootmap *map);
/* Caller must first settle all File/close references (sysfilerefconnection=0).
   Directory must be DRAINING with no active bootstrap owner, bootstrap record
   FREE and no occupied pages. Unmap, free MEM, then free owned CALL; failures
   retain remaining exact responsibility for retry. Never frees SELF_SPACE or
   another caller's borrowed CALL. Does not evict/forget the key. */
isize sysconnworkclean(Connwork *work, Conncache *cache);
#endif
