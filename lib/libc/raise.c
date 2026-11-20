#include <signal.h>
#include <unistd.h>
#include <errno.h>
#include "private/syscall.h"
int
raise(int signum)
{
  Sigsel selector;
  isize  error;
  /* POSIX permits EINVAL for the null signal here. kill's null-signal
     existence query is a separate interface, not a group/self shortcut. */
  if (signum <= 0 || signum > SIGNAL_COUNT || !(SIGNAL_SUPPORTED & ((u64) 1 << (signum - 1)))) {
    errno = EINVAL;
    return -1;
  }
  selector.kind = SIGNAL_SELECT_PID;
  selector.target = getpid();
  error = __sys_sigsend(&selector, (u64) signum);
  if (!error)
    return 0;
  switch (error) {
  case -ERR_INVALID:
    errno = EINVAL;
    break;
  case -ERR_BUSY:
  case -ERR_LIMIT:
  case -ERR_CAPACITY:
    errno = EAGAIN;
    break;
  case -ERR_NOMEM:
    errno = ENOMEM;
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
