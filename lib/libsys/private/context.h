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
#ifndef __ASSEMBLER__
#include "syscall.h"
#include "conn.h"
typedef struct Rctx  Rctx;
typedef struct Rbank Rbank;
typedef struct Rkey  Rkey;
struct Rctx
{
  volatile u64 phase;
  u64          sequence, number;
  isize        status;
  Sysret       result;
  u64          args[7];
  volatile u64 active;
  u64          reserved[44];
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
   File/Op/Bootstrap references remain caller responsibilities in this slice.
   Prepare/capture/result/finish require caller caught-signal exclusion. */
/* SIGRETURN keeps its own NORETURN stub, never leaves a live Rctx behind. */
isize sysctxinit(Rbank *bank, Connpeer self);
isize sysctxprepare(Rbank *bank, Connpeer self, u32 layer, u64 number, const u64 args[7],
                    Rkey *out);
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
/* Architecture-private register decoder and actual assembly entry. Bounds
   supplied only by sysctxcapture from these fixed link symbols. */
isize           sysctxdecode(Rctx *context, void *trap, u64 begin, u64 end, u64 resume);
isize           __sys_ctxenter(Rctx *context);
extern const u8 __sys_ctxreturn_begin[], __sys_ctxreturn_end[], __sys_ctxresume[];
#endif
#endif
