#ifndef _LIBSYS_CONTEXT_H
#define _LIBSYS_CONTEXT_H
#define RCTX_PHASE          0
#define RCTX_SEQUENCE       8
#define RCTX_NUMBER         16
#define RCTX_STATUS         24
#define RCTX_RESULT         32
#define RCTX_ARGS           96
#define RCTX_ACTIVE         152
#define RCTX_FREE           0
#define RCTX_PREPARED       1
#define RCTX_WAITING        2
#define RCTX_DONE           3
#define RCTX_CLEANING       4
#define RCTX_LAYERS         3
#define RCTX_BYTES          512
#define RCTX_SNAPSHOT_BYTES 16384
#define RCTX_INPUT_ALIGN    8
#ifndef __ASSEMBLER__
#include "syscall.h"
#include "operation.h"
#include "argument.h"
#define RCTX_ROUTE_NONE 0
#define RCTX_ROUTE_BOOT 1
#define RCTX_ROUTE_OP   2
typedef struct Rctx   Rctx;
typedef struct Rbank  Rbank;
typedef struct Rkey   Rkey;
typedef struct Rroute Rroute;
struct Rroute
{
  u64        kind, delivered;
  Bootstrap *bootstrap;
  Connwork  *work;
  Conncache *cache;
  Connclaim  claim;
  Opkey      operation;
  Handle     rpc;
  u64        interruptible, snapshot_offset, snapshot_bytes, snapshot_saved;
  Filepool  *pool;
  isize      capture_status;
};
struct Rctx
{
  volatile u64 phase;
  u64          sequence, number;
  isize        status;
  Sysret       result;
  u64          args[7];
  volatile u64 active;
  Rroute       route;
  u64          input_bytes, input_saved;
  u32          input_kind, input_argc, input_envc, input_reserved;
  u64          reserved[19];
};
struct Rbank
{
  Connpeer self;
  u64      sequence;
  Rctx     contexts[RCTX_LAYERS];
  u8       snapshots[RCTX_LAYERS][RCTX_SNAPSHOT_BYTES];
};
struct Rkey
{
  u64 sequence;
  u32 layer, reserved;
};
STATIC_ASSERT(sizeof(Rctx) == RCTX_BYTES, runtime_context_size);
STATIC_ASSERT(offsetof(Rctx, phase) == RCTX_PHASE, runtime_context_phase);
STATIC_ASSERT(offsetof(Rctx, sequence) == RCTX_SEQUENCE, runtime_context_sequence);
STATIC_ASSERT(offsetof(Rctx, number) == RCTX_NUMBER, runtime_context_number);
STATIC_ASSERT(offsetof(Rctx, result) == RCTX_RESULT, runtime_context_result);
STATIC_ASSERT(offsetof(Rctx, args) == RCTX_ARGS, runtime_context_arguments);
STATIC_ASSERT(offsetof(Rctx, active) == RCTX_ACTIVE, runtime_context_active);
STATIC_ASSERT(offsetof(Rctx, status) == RCTX_STATUS, runtime_context_status);
STATIC_ASSERT(sizeof(Rkey) == 16, runtime_context_key);
STATIC_ASSERT(sizeof(Rbank) == 24 + RCTX_LAYERS * (RCTX_BYTES + RCTX_SNAPSHOT_BYTES),
              runtime_bank_size);
/* Fresh private preallocated Image storage, never initialize live obligations.
   Three metadata/snapshot layers, no global allocation. Layer selection and
   IPC route registration binds existing File/Op/Bootstrap responsibility;
   it does not allocate or duplicate those references.
   Prepare/capture/result/finish require caller caught-signal exclusion. */
/* SIGRETURN keeps its own NORETURN stub, never leaves a live Rctx behind. */
isize sysctxinit(Rbank *bank, Connpeer self);
isize sysctxprepare(Rbank *bank, Connpeer self, u32 layer, u64 number, const u64 args[7],
                    Rkey *out);
/* Protected, inactive PREPARED only. Checked copy accepts input once; failed
   copies do not publish bytes. Never retains the original application pointer.
   Input and rounded output workspace share the existing per-layer 16KiB.
   Borrowed view is valid for this incarnation until abort/finish. */
isize sysctxinput(Rbank *bank, Connpeer self, Rkey key, const void *source, u64 bytes);
isize sysctxinputview(Rbank *bank, Connpeer self, Rkey key, const void **out, u64 *bytes);
/* One complete argv/env capture directly into this layer's existing prefix.
   Argsnap borrows that prefix and expires with the context incarnation. */
isize sysctxarguments(Rbank *bank, Connpeer self, Rkey key, const char *const *argv,
                      const char *const *envp, Argsnap *out);
isize sysctxargumentview(Rbank *bank, Connpeer self, Rkey key, Argsnap *out);
/* Continue an unbound raw context after its previous tuple moved out. Keeps
   accepted input and incarnation; never replays a bound service operation. */
isize sysctxrearm(Rbank *bank, Connpeer self, Rkey key, u64 number, const u64 args[7]);
/* Cancel only inactive raw preparation. External Op/Bootstrap/RPC responsibility
   remains owned there and must be rolled back or continued separately. */
isize sysctxabort(Rbank *bank, Connpeer self, Rkey key);
/* Internally masks only activation/completion changes, restores mask before
   actual syscall, and returns the saved native status. A signal may move raw
   result to existing responsibility records, but cannot finish active Rctx. */
isize sysctxinvoke(Rbank *bank, Connpeer self, Rkey key);
/* Registered exact raw syscall return window only. Captures all native return
   registers and redirects saved PC to assembly resume point; never scans C
   stack. Returns 1 captured, 0 outside this window, negative invalid state.
   Does not ACK, cancel or install fd/resources. Kernel sigreturn still checks
   the complete user frame. Raw trap storage is supplied by its trampoline. */
isize sysctxcapture(Rbank *bank, Connpeer self, Rkey key, void *trap);
/* Unique raw tuple move before reuse. Read-only values may remain in the old
   record for diagnostics, but CLEANING no longer owns a second tuple. */
isize sysctxresult(Rbank *bank, Connpeer self, Rkey key, isize *status, Sysret *out);
isize sysctxfinish(Rbank *bank, Connpeer self, Rkey key);
/* Library-private lookup shared with the IPC route module. Borrowed metadata,
   valid only under caller signal exclusion; never an ownership transfer. */
isize sysctxlookup(Rbank *bank, Connpeer self, Rkey key, Rctx **out);
/* Architecture-private register decoder and actual assembly entry. Bounds
   supplied only by sysctxcapture from these fixed link symbols. */
isize           sysctxdecode(Rctx *context, void *trap, u64 begin, u64 end, u64 resume);
isize           __sys_ctxenter(Rctx *context);
extern const u8 __sys_ctxreturn_begin[], __sys_ctxreturn_end[], __sys_ctxresume[];
#endif
#endif
