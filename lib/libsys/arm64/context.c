#include "private/context.h"
#include "private/timer.h"
#include <abi/arm64/trap.h>
#include <abi/signal.h>
STATIC_ASSERT(sizeof(Trapframe) == SIGNAL_FRAME_FP - SIGNAL_FRAME_TF, runtime_signal_trap_size);
void
syssleepskip(void *raw, u64 begin, u64 end, u64 resume)
{
  Trapframe *trap = raw;
  if (trap->pc >= begin && trap->pc < end)
    trap->pc = resume;
}
isize
sysforkdecode(const void *raw, u64 begin, u64 end, Forkret *out)
{
  const Trapframe *trap = raw;
  if (!trap || !out || begin >= end || (begin | end) & 3)
    return -ERR_INVALID;
  if (trap->pc < begin || trap->pc >= end || trap->pc & 3 || trap->x[0] || trap->x[1])
    return 0;
  out->construct = 0;
  out->pid = (Handle) trap->x[2];
  out->fix = (const void *) trap->x[3];
  return 1;
}
isize
sysctxdecode(Rctx *context, void *raw, u64 begin, u64 end, u64 resume)
{
  Trapframe *trap = raw;
  u32        i;
  if (!context || !trap || begin >= end || (begin & 3) || (end & 3) || resume != end)
    return -ERR_INVALID;
  if (trap->pc < begin || trap->pc >= end || (trap->pc - begin) & 3 ||
      trap->x[19] != (u64) (uintptr_t) context)
    return 0;
  if (!context->active || (context->phase != RCTX_WAITING && context->phase != RCTX_DONE))
    return -ERR_STATE;
  if (context->phase == RCTX_WAITING) {
    context->status = (isize) trap->x[0];
    for (i = 0; i < 7; i++)
      context->result.values[i] = trap->x[1 + i];
    context->result.image = trap->x[9];
    context->phase = RCTX_DONE;
  }
  trap->pc = resume;
  return 1;
}
