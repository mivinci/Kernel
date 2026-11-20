#ifndef _LIBSYS_CONTEXTIPC_H
#define _LIBSYS_CONTEXTIPC_H
#include "context.h"
/* Short caller-protected registration before sysctxinvoke. The destination
   and its exact claim/key stay exclusively bound until handoff. No waiting,
   allocation, ACK, cancellation or application output pointer. CALL is always
   asynchronous; options select only native MEM/CALL receive consent. */
isize sysctxbootcall(Rbank *bank, Connpeer self, u32 layer, Bootstrap *record, Connclaim claim,
                     Handle call, u64 options, Rkey *out);
isize sysctxbootwait(Rbank *bank, Connpeer self, u32 layer, Bootstrap *record, Connclaim claim,
                     Rkey *out);
isize sysctxopcall(Rbank *bank, Connpeer self, u32 layer, Connwork *work, Conncache *cache,
                   const Opkey *operation, u64 options, Rkey *out);
isize sysctxopwait(Rbank *bank, Connpeer self, u32 layer, Connwork *work, Conncache *cache,
                   const Opkey *operation, Rkey *out);
/* After return-window capture or normal return, under caught-signal exclusion.
   Moves raw ownership once, is idempotent after that move, and keeps DONE on
   failure. Transient wait errors keep the original RPC in its destination.
   Native status remains available to the active invocation's continuation.
   Raw sysctxresult is forbidden for a bound route. Finish remains separate. */
isize sysctxhandoff(Rbank *bank, Connpeer self, Rkey key);
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
#endif
