#include <errno.h>
#include <pthread.h>
#include <signal.h>
#include "private/syscall.h"

int
sigpending(sigset_t *set)
{
  sigset_t result = {0};
  u64      pending;
  if (!set) {
    errno = EFAULT;
    return -1;
  }
  if (__sys_sigpending(&pending)) {
    errno = EIO;
    return -1;
  }
  result.__bits[0] = pending;
  if (__sys_copy(set, &result, sizeof(result))) {
    errno = EFAULT;
    return -1;
  }
  return 0;
}

int
sigsuspend(const sigset_t *set)
{
  isize status;
  u64   mask;
  if (!set || __sys_copy(&mask, set, sizeof(mask))) {
    errno = EFAULT;
    return -1;
  }
  status = __sys_sigsuspend(mask & SIGNAL_SUPPORTED);
  errno = status == -ERR_INTERRUPTED ? EINTR : EIO;
  return -1;
}

/* The native ABI has no libc-private signals in its current namespace. */
void
__block_all_sigs(void *saved)
{
  if (__sys_sigmask(SIGNAL_BLOCK, SIGNAL_SUPPORTED, saved))
    __builtin_trap();
}

void
__block_app_sigs(void *saved)
{
  __block_all_sigs(saved);
}

void
__restore_sigs(void *saved)
{
  u64 ignored;
  if (__sys_sigmask(SIGNAL_SETMASK, *(const u64 *) saved, &ignored))
    __builtin_trap();
}

int
pthread_sigmask(int how, const sigset_t *restrict set, sigset_t *restrict old)
{
  u64 native = SIGNAL_BLOCK, mask = 0, previous;
  if (set) {
    switch (how) {
    case SIG_BLOCK:
      native = SIGNAL_BLOCK;
      break;
    case SIG_UNBLOCK:
      native = SIGNAL_UNBLOCK;
      break;
    case SIG_SETMASK:
      native = SIGNAL_SETMASK;
      break;
    default:
      return EINVAL;
    }
    mask = set->__bits[0] & SIGNAL_SUPPORTED;
  }
  if (__sys_sigmask(native, mask, &previous))
    return EIO;
  if (old) {
    *old = (sigset_t){0};
    old->__bits[0] = previous;
  }
  return 0;
}
