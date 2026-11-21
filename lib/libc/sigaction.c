#include <errno.h>
#include <signal.h>
#include <abi/proc.h>
#include "private/sigentry.h"
#include "private/syscall.h"

#define ACTION_FLAGS                                                                               \
  (SA_NODEFER | SA_RESETHAND | SA_NOCLDSTOP | SA_NOCLDWAIT | SA_SIGINFO | SA_RESTART)

static volatile u32 depth;

u32
__libc_sigdepth(void)
{
  return depth;
}

static int
faultcode(int signum, u64 reason)
{
  switch (signum) {
  case SIGSEGV:
    if (reason == SIGNAL_FAULT_MAPERR)
      return SEGV_MAPERR;
    if (reason == SIGNAL_FAULT_ACCERR)
      return SEGV_ACCERR;
    break;
  case SIGBUS:
    if (reason == SIGNAL_FAULT_ALIGN)
      return BUS_ADRALN;
    if (reason == SIGNAL_FAULT_BUSERR)
      return BUS_ADRERR;
    break;
  case SIGILL:
    if (reason == SIGNAL_FAULT_ILLEGAL)
      return ILL_ILLOPC;
    break;
  case SIGFPE:
    switch (reason) {
    case SIGNAL_FAULT_FP_INVALID:
      return FPE_FLTINV;
    case SIGNAL_FAULT_FP_DIVZERO:
      return FPE_FLTDIV;
    case SIGNAL_FAULT_FP_OVERFLOW:
      return FPE_FLTOVF;
    case SIGNAL_FAULT_FP_UNDERFLOW:
      return FPE_FLTUND;
    case SIGNAL_FAULT_FP_INEXACT:
      return FPE_FLTRES;
    }
    break;
  }
  return SI_KERNEL;
}

static int
childcode(u64 reason, u64 status)
{
  switch (reason) {
  case WAIT_EXITED:
    return !(status & WAIT_SIGNALLED) ? CLD_EXITED : status & WAIT_CORE ? CLD_DUMPED : CLD_KILLED;
  case WAIT_STOPPED:
    return CLD_STOPPED;
  case WAIT_CONTINUED:
    return CLD_CONTINUED;
  }
  return SI_KERNEL;
}

static NORETURN void
trampoline(u64 signum, const Siginfo *info, void *context, u64 function, u64 frame, u64 flags)
{
  const Sighead *head = (const Sighead *) (uintptr_t) frame;
  u32            layer = (u32) head->depth;
  u64            ignored;
  int            saved = errno;
  if (sysctxsigentry(head, flags))
    __sys_exit(128 + SIGSEGV);
  depth = layer;
  if (flags & SIGNAL_SIGINFO) {
    void (*handler)(int, siginfo_t *, void *) =
        (void (*)(int, siginfo_t *, void *))(uintptr_t) function;
    siginfo_t converted = {0};
    converted.si_signo = (int) signum;
    converted.si_code = SI_KERNEL;
    if (info->code == SIGNAL_SOURCE_USER || info->code == SIGNAL_SOURCE_CHILD) {
      converted.si_pid = (pid_t) info->pid;
      converted.si_uid = info->uid;
      if (info->code == SIGNAL_SOURCE_CHILD) {
        converted.si_code = childcode(info->reason, info->status);
        converted.si_status =
            info->reason == WAIT_CONTINUED ? SIGCONT : (int) (info->status & 0xff);
      } else
        converted.si_code = SI_USER;
    } else if (info->code == SIGNAL_SOURCE_FAULT) {
      converted.si_code = faultcode((int) signum, info->reason);
      converted.si_addr = (void *) (uintptr_t) info->address;
    }
    handler((int) signum, &converted, context);
  } else {
    void (*handler)(int) = (void (*)(int))(uintptr_t) function;
    handler((int) signum);
  }
  if (__sys_sigmask(SIGNAL_BLOCK, SIGNAL_SUPPORTED, &ignored))
    __sys_exit(128 + SIGSEGV);
  depth = layer - 1;
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
  uintptr_t        handler;
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
    handler =
        input.sa_flags & SA_SIGINFO ? (uintptr_t) input.sa_sigaction : (uintptr_t) input.sa_handler;
    if (handler == (uintptr_t) SIG_DFL)
      native.kind = SIGNAL_DEFAULT;
    else if (handler == (uintptr_t) SIG_IGN)
      native.kind = SIGNAL_IGNORE;
    else {
      native.kind = SIGNAL_HANDLER;
      native.handler = handler;
      native.trampoline = (uintptr_t) trampoline;
    }
  }
  error = __sys_sigaction((u64) signum, action ? &native : NULL, old ? &previous : NULL);
  if (error || !old)
    return error ? failure(error) : 0;
  if (previous.kind == SIGNAL_HANDLER &&
      (previous.trampoline != (uintptr_t) trampoline || previous.flags & ~(u64) ACTION_FLAGS))
    return failure(-ERR_NOTSUP);
  if (previous.kind == SIGNAL_HANDLER && (previous.flags & SIGNAL_SIGINFO))
    output.sa_sigaction = (void (*)(int, siginfo_t *, void *))(uintptr_t) previous.handler;
  else
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
