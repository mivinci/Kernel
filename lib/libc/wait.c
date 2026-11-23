#include <sys/wait.h>
#include <errno.h>
#include "private/syscall.h"
STATIC_ASSERT(sizeof(pid_t) == sizeof(int32_t), wait_pid_width);
STATIC_ASSERT(sizeof(int) == 4, wait_status_width);
static pid_t
failure(isize error)
{
  switch (error) {
  case -ERR_CHILD:
  case -ERR_BAD_HANDLE:
  case -ERR_STALE:
    errno = ECHILD;
    break;
  case -ERR_INTERRUPTED:
    errno = EINTR;
    break;
  case -ERR_INVALID:
    errno = EINVAL;
    break;
  case -ERR_NOTSUP:
  case -ERR_NOSYS:
    errno = ENOSYS;
    break;
  case -ERR_BUSY:
    errno = EAGAIN;
    break;
  default:
    errno = EIO;
  }
  return (pid_t) -1;
}
pid_t
waitpid(pid_t selector, int *status, int options)
{
  Handle pid;
  u64    kind, code, sequence;
  isize  error;
  int    encoded;
  if (options & ~(WNOHANG | WUNTRACED | WCONTINUED))
    return failure(-ERR_INVALID);
  error = __sys_wait(selector, (u64) options, &pid, &kind, &code, &sequence);
  if (error)
    return failure(error);
  if (!pid)
    return 0;
  if (kind == WAIT_EXITED)
    encoded = code & WAIT_SIGNALLED ? (int) (code & 0x7f) | (code & WAIT_CORE ? 0x80 : 0)
                                    : (int) ((code & 0xff) << 8);
  else if (kind == WAIT_STOPPED)
    encoded = (int) ((code & 0xff) << 8) | 0x7f;
  else if (kind == WAIT_CONTINUED)
    encoded = 0xffff;
  else
    return failure(-ERR_STATE);
  /* Native delivery already consumed this exact result. A bad application
     output never retries wait or restores it to the parent's result set. */
  if (status) {
    error = __sys_copy(status, &encoded, sizeof(encoded));
    if (error) {
      errno = error == -ERR_FAULT || error == -ERR_INVALID ? EFAULT : EIO;
      return (pid_t) -1;
    }
  }
  return pid;
}
pid_t
wait(int *status)
{
  return waitpid((pid_t) -1, status, 0);
}
