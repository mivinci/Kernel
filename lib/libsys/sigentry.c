#include "private/contextipc.h"
#include "private/fork.h"
#include "private/sigentry.h"
#include <abi/syscall.h>
#include <abi/time.h>

static Rbank   *signalbank;
static Connpeer prepared_self;
static u64      prepared[SIGNAL_DEPTH];

/* The bank belongs to the current image and lives until image replacement. */
isize
sysctxsigbind(Rbank *bank)
{
  Connpeer self;
  Handle   parent;
  u64      mask, ignored;
  isize    error, restored;
  if (!bank)
    return -ERR_INVALID;
  error = __sys_getid(&self.pid, &self.image, &parent);
  if (error)
    return error;
  error = __sys_sigmask(SIGNAL_BLOCK, SIGNAL_SUPPORTED, &mask);
  if (error)
    return error;
  if (bank->self.pid != self.pid || bank->self.image != self.image)
    error = -ERR_STALE;
  else if (signalbank && signalbank != bank)
    error = -ERR_BUSY;
  else
    signalbank = bank;
  restored = __sys_sigmask(SIGNAL_SETMASK, mask, &ignored);
  return error ? error : restored;
}

static u64
mononow(void)
{
  const u64 second = UINT64_C(1000000000);
  int64_t   seconds;
  u64       ns;
  if (__sys_clock_gettime(CLOCK_MONOTONIC, &seconds, &ns) || seconds < 0 || ns >= second ||
      (u64) seconds > (UINT64_MAX - ns) / second)
    return UINT64_MAX; /* Still capture the result. No cancellation deadline can be formed. */
  return (u64) seconds * second + ns;
}

static int
validhead(const Sighead *head)
{
  return head->magic == SIGNAL_FRAME_MAGIC &&
         (head->bytes == SIGNAL_FRAME_ARM_BYTES || head->bytes == SIGNAL_FRAME_RV_BYTES) &&
         head->cookie && head->depth && head->depth <= SIGNAL_DEPTH &&
         (!!head->previous == (head->depth > 1));
}

static void
prepareframe(Rbank *bank, Connpeer self, const Sighead *head, void *trap)
{
  Rkey  key = {0, (u32) head->depth - 1, 0};
  Rctx *context = &bank->contexts[key.layer];
  if (prepared[key.layer] == head->cookie)
    return;
  prepared[key.layer] = head->cookie;
  if (context->phase == RCTX_FREE)
    return;
  /* Native child wait can replace EINTR at sigreturn. Leave its assembly
     return window intact so it captures the final result. */
  if (context->number == SYS_WAIT && context->phase == RCTX_WAITING)
    return;
  key.sequence = context->sequence;
  /* Deferred transport work remains with the original route on failure. */
  sysctxsignal(bank, self, key, trap, context->route.interruptible ? mononow() : 0);
}

static isize
preparesignal(Rbank *bank, const Sighead *frame)
{
  Sighead  outer;
  void    *trap = (u8 *) frame + SIGNAL_FRAME_TF;
  void    *outer_trap = NULL;
  Connpeer self;
  Handle   parent;
  u32      i;
  isize    error = __sys_getid(&self.pid, &self.image, &parent);
  if (error)
    return error;
  if (!validhead(frame))
    return -ERR_INVALID;
  if (frame->previous) {
    u64 previous = frame->previous;
    if ((previous & 15) || previous < BOOT_USER_LO || previous >= BOOT_USER_HI ||
        sizeof(outer) > BOOT_USER_HI - previous)
      return -ERR_INVALID;
    error = __sys_copy(&outer, (const void *) (uintptr_t) previous, sizeof(outer));
    if (error)
      return error;
    if (!validhead(&outer) || outer.depth != frame->depth - 1 || outer.bytes != frame->bytes ||
        outer.cookie >= frame->cookie || outer.bytes > BOOT_USER_HI - previous)
      return -ERR_INVALID;
    outer_trap = (void *) (uintptr_t) (previous + SIGNAL_FRAME_TF);
  }
  if (bank->self.pid != self.pid || bank->self.image != self.image) {
    error = sysforksignal(bank, trap, outer_trap);
    if (error)
      return error;
  }
  if (prepared_self.pid != self.pid || prepared_self.image != self.image) {
    for (i = 0; i < SIGNAL_DEPTH; i++)
      prepared[i] = 0;
    prepared_self = self;
  }
  if (outer_trap)
    prepareframe(bank, self, &outer, outer_trap);
  prepareframe(bank, self, frame, trap);
  return 0;
}

isize
sysctxsigentry(const Sighead *frame)
{
  u64   mask, ignored;
  isize error, restored;
  if (!signalbank)
    return 0;
  error = __sys_sigmask(SIGNAL_BLOCK, SIGNAL_SUPPORTED, &mask);
  if (error)
    return error;
  error = preparesignal(signalbank, frame);
  restored = __sys_sigmask(SIGNAL_SETMASK, mask, &ignored);
  return error ? error : restored;
}
