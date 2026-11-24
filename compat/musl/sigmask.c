#include <errno.h>
#include <pthread.h>
#include <signal.h>
#include <abi/signal.h>

extern isize __sys_sigmask(u64, u64, u64 *);

int
pthread_sigmask(int how, const sigset_t *restrict set, sigset_t *restrict old)
{
  u64 native = SIGNAL_BLOCK, mask = 0, previous;
  if (set) {
    switch (how) {
    case SIG_BLOCK: native = SIGNAL_BLOCK; break;
    case SIG_UNBLOCK: native = SIGNAL_UNBLOCK; break;
    case SIG_SETMASK: native = SIGNAL_SETMASK; break;
    default: return EINVAL;
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
