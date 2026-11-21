#include <errno.h>
#include <signal.h>
#include "private/syscall.h"

#define ACTION_FLAGS (SA_NODEFER | SA_RESETHAND | SA_NOCLDSTOP | SA_NOCLDWAIT)

static NORETURN void
trampoline(u64 signum, const Siginfo *info, void *context, u64 function, u64 frame, u64 flags)
{
  void (*handler)(int) = (void (*)(int))(uintptr_t) function;
  int saved = errno;
  UNUSED(info);
  UNUSED(context);
  UNUSED(flags);
  handler((int) signum);
  errno = saved;
  __sys_sigreturn((void *) frame);
}

static int
failure(isize error)
{
  errno = error == -ERR_INVALID  ? EINVAL
          : error == -ERR_FAULT  ? EFAULT
          : error == -ERR_NOTSUP ? ENOTSUP
                                 : EIO;
  return -1;
}

int
sigaction(int signum, const struct sigaction *action, struct sigaction *old)
{
  struct sigaction input, output = {0};
  Sigact           native = {0}, previous;
  isize            error;
  if (signum <= 0 || signum > SIGNAL_COUNT || !(SIGNAL_SUPPORTED & (1UL << (signum - 1))))
    return failure(-ERR_INVALID);
  if (action) {
    error = __sys_copy(&input, action, sizeof(input));
    if (error)
      return failure(error);
    if (input.sa_flags & ~ACTION_FLAGS)
      return failure(-ERR_INVALID);
    native.mask = input.sa_mask;
    native.flags = (u64) input.sa_flags;
    if (input.sa_handler == SIG_DFL)
      native.kind = SIGNAL_DEFAULT;
    else if (input.sa_handler == SIG_IGN)
      native.kind = SIGNAL_IGNORE;
    else {
      native.kind = SIGNAL_HANDLER;
      native.handler = (uintptr_t) input.sa_handler;
      native.trampoline = (uintptr_t) trampoline;
    }
  }
  error = __sys_sigaction((u64) signum, action ? &native : NULL, old ? &previous : NULL);
  if (error || !old)
    return error ? failure(error) : 0;
  if (previous.kind == SIGNAL_HANDLER &&
      (previous.trampoline != (uintptr_t) trampoline || previous.flags & ~(u64) ACTION_FLAGS))
    return failure(-ERR_NOTSUP);
  output.sa_handler = previous.kind == SIGNAL_DEFAULT ? SIG_DFL
                      : previous.kind == SIGNAL_IGNORE
                          ? SIG_IGN
                          : (void (*)(int))(uintptr_t) previous.handler;
  output.sa_mask = previous.mask;
  output.sa_flags = (int) previous.flags;
  /* Do not undo the kernel's committed action if fetching the old action fails. */
  error = __sys_copy(old, &output, sizeof(output));
  return error ? failure(error) : 0;
}
