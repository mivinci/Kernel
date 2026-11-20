#ifndef _LIBSYS_CONNECTION_H
#define _LIBSYS_CONNECTION_H
#include "bootstrap.h"
#include "file.h"
#include "fileref.h"
#define SYSFILE_CREATE_PREPARED 1
#define SYSFILE_CREATE_CAPTURED 2
#define SYSFILE_CREATE_RESOLVED 3
#define SYSFILE_RESULT_REVOKED  2
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
  u32       phase, count, disposition, reserved;
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
isize sysconnworkreserve(Connwork *work, Conncache *cache, u32 context, u32 span, Filelease *out);
isize sysconnworkrelease(Connwork *work, Conncache *cache, Filelease expected);
/* Caller must first settle all File/close references (sysfilerefconnection=0).
   Directory must be DRAINING with no active bootstrap owner, bootstrap record
   FREE and no occupied pages. Unmap, free MEM, then free owned CALL; failures
   retain remaining exact responsibility for retry. Never frees SELF_SPACE or
   another caller's borrowed CALL. Does not evict/forget the key. */
isize sysconnworkclean(Connwork *work, Conncache *cache);
#endif
