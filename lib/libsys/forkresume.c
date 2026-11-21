#include "private/fork.h"
#include "private/sigentry.h"

typedef struct Forkflight Forkflight;
struct Forkflight
{
  const Forkresume *plan;
  Connpeer          parent;
  Forkret           result;
  Forkdone          done;
  u32               released;
};

static Forkflight flights[RCTX_LAYERS];
static u32        depth;

static isize
repair(Forkflight *flight, const Forkret *result)
{
  const Forkresume *plan = flight->plan;
  u64               mask, ignored;
  isize             status, restore;
  if ((u64) result->fix & PAGE_MASK)
    return -ERR_INVALID;
  status = __sys_sigmask(SIGNAL_BLOCK, SIGNAL_SUPPORTED, &mask);
  if (status)
    return status;
  status = sysforkrepair(&flight->done, &plan->state, flight->parent, result, plan->available,
                         plan->files, plan->file_count, plan->detach);
  if (!status && !flight->released) {
    status = __sys_unmap(plan->space, (u64) result->fix);
    if (!status)
      flight->released = 1;
  }
  restore = __sys_sigmask(SIGNAL_SETMASK, mask, &ignored);
  return status ? status : restore;
}

isize
sysforkresume(Handle authority, u64 reserve, const Forkresume *plan, Forkret *out)
{
  Forkflight *flight;
  u64         mask, ignored;
  isize       error;
  if (!plan || !out || !plan->state.contexts || !plan->state.fds || !plan->state.files ||
      !plan->state.connections || plan->space <= 0 || plan->available < FIX_BASE_BYTES ||
      plan->available > BOOT_MAX_BYTES || (plan->file_count && !plan->files))
    return -ERR_INVALID;
  error = sysctxsigbind(plan->state.contexts);
  if (error)
    return error;
  error = __sys_sigmask(SIGNAL_BLOCK, SIGNAL_SUPPORTED, &mask);
  if (error)
    return error;
  if (depth == ARRAY_SIZE(flights)) {
    __sys_sigmask(SIGNAL_SETMASK, mask, &ignored);
    return -ERR_CAPACITY;
  }
  flight = &flights[depth];
  flight->plan = plan;
  flight->parent = plan->state.contexts->self;
  flight->result = (Forkret){0, 0, NULL};
  flight->done.child.pid = 0;
  flight->released = 0;
  depth++;
  if (__sys_sigmask(SIGNAL_SETMASK, mask, &ignored))
    __sys_exit(128 + SIGNAL_SEGV);
  error = __sys_forkprep(authority, reserve, &flight->result);
  if (!error && !flight->result.construct && repair(flight, &flight->result))
    __sys_exit(128 + SIGNAL_SEGV);
  if (__sys_sigmask(SIGNAL_BLOCK, SIGNAL_SUPPORTED, &mask))
    __sys_exit(128 + SIGNAL_SEGV);
  *out = flight->result;
  out->fix = NULL;
  depth--;
  if (__sys_sigmask(SIGNAL_SETMASK, mask, &ignored))
    __sys_exit(128 + SIGNAL_SEGV);
  return error;
}

isize
sysforksignal(Rbank *bank, const void *trap, const void *outer)
{
  const u64   begin = (u64) (uintptr_t) __sys_forkreturn_begin;
  const u64   end = (u64) (uintptr_t) __sys_forkreturn_end;
  Forkflight *flight;
  Forkret     result;
  isize       found;
  if (!depth)
    return -ERR_STALE;
  flight = &flights[depth - 1];
  if (flight->plan->state.contexts != bank || bank->self.pid != flight->parent.pid ||
      bank->self.image != flight->parent.image)
    return -ERR_STALE;
  found = sysforkdecode(trap, begin, end, &result);
  if (!found && outer)
    found = sysforkdecode(outer, begin, end, &result);
  if (found < 0)
    return found;
  /* Outside the window, the assembly has already published the tuple. */
  return repair(flight, found ? &result : &flight->result);
}
