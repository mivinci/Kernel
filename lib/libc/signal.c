#include <signal.h>
#include <errno.h>
#include "private/syscall.h"
STATIC_ASSERT(sizeof(sigset_t) == 8, libc_signal_mask_width);
static int
failure(isize error)
{
  switch (error) {
  case -ERR_INVALID:
    errno = EINVAL;
    break;
  case -ERR_FAULT:
    errno = EFAULT;
    break;
  case -ERR_INTERRUPTED:
    errno = EINTR;
    break;
  case -ERR_BUSY:
    errno = EAGAIN;
    break;
  case -ERR_NOTSUP:
  case -ERR_NOSYS:
    errno = ENOSYS;
    break;
  default:
    errno = EIO;
  }
  return -1;
}
static int
valid(int signum)
{
  return signum > 0 && signum <= SIGNAL_COUNT && (SIGNAL_SUPPORTED & ((u64) 1 << (signum - 1)));
}
int
sigemptyset(sigset_t *set)
{
  if (!set)
    return failure(-ERR_INVALID);
  *set = 0;
  return 0;
}
int
sigfillset(sigset_t *set)
{
  if (!set)
    return failure(-ERR_INVALID);
  *set = SIGNAL_SUPPORTED;
  return 0;
}
int
sigaddset(sigset_t *set, int signum)
{
  if (!set || !valid(signum))
    return failure(-ERR_INVALID);
  *set |= (u64) 1 << (signum - 1);
  return 0;
}
int
sigdelset(sigset_t *set, int signum)
{
  if (!set || !valid(signum))
    return failure(-ERR_INVALID);
  *set &= ~((u64) 1 << (signum - 1));
  return 0;
}
int
sigismember(const sigset_t *set, int signum)
{
  if (!set || !valid(signum))
    return failure(-ERR_INVALID);
  return (*set & ((u64) 1 << (signum - 1))) != 0;
}
int
sigprocmask(int how, const sigset_t *set, sigset_t *old)
{
  u64   mask = 0, previous;
  isize error;
  if (set) {
    if (how < SIG_BLOCK || how > SIG_SETMASK)
      return failure(-ERR_INVALID);
    error = __sys_copy(&mask, set, sizeof(mask));
    if (error)
      return failure(error);
  } else {
    how = SIG_BLOCK;
  }
  error = __sys_sigmask((u64) how, mask, &previous);
  if (error)
    return failure(error);
  /* The native mask change already committed. A bad old-output pointer does
     not restore a mask possibly changed again by a handler in the meantime. */
  if (old && (error = __sys_copy(old, &previous, sizeof(previous))))
    return failure(error);
  return 0;
}
int
sigpending(sigset_t *set)
{
  u64   pending;
  isize error;
  if (!set)
    return failure(-ERR_FAULT);
  error = __sys_sigpending(&pending);
  if (!error)
    error = __sys_copy(set, &pending, sizeof(pending));
  return error ? failure(error) : 0;
}
int
sigsuspend(const sigset_t *set)
{
  u64   mask;
  isize error;
  if (!set)
    return failure(-ERR_FAULT);
  error = __sys_copy(&mask, set, sizeof(mask));
  if (!error)
    error = __sys_sigsuspend(mask);
  return failure(error ? error : -ERR_STATE);
}
