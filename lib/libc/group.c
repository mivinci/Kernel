#include <unistd.h>
#include <errno.h>
#include "private/syscall.h"
STATIC_ASSERT(sizeof(pid_t) == sizeof(Handle), group_pid_width);
static pid_t
failure(isize error, u32 set)
{
  switch (error) {
  case -ERR_INVALID:
    errno = EINVAL;
    break;
  case -ERR_BAD_HANDLE:
  case -ERR_STALE:
    errno = ESRCH;
    break;
  case -ERR_DENIED:
    errno = EPERM;
    break;
  case -ERR_STATE:
    errno = set ? EACCES : EIO;
    break;
  case -ERR_BUSY:
  case -ERR_LIMIT:
  case -ERR_CAPACITY:
    errno = EAGAIN;
    break;
  case -ERR_NOMEM:
    errno = ENOMEM;
    break;
  case -ERR_OVERFLOW:
    errno = EOVERFLOW;
    break;
  case -ERR_NOTSUP:
  case -ERR_NOSYS:
    errno = ENOSYS;
    break;
  default:
    errno = EIO;
  }
  return (pid_t) -1;
}
static pid_t
query(pid_t pid, u32 session)
{
  Handle pgid, sid;
  isize  error;
  if (pid < 0)
    return failure(-ERR_INVALID, 0);
  error = __sys_getpg(pid, &pgid, &sid);
  if (error)
    return failure(error, 0);
  return session ? sid : pgid;
}
pid_t
getpgrp(void)
{
  Handle pgid, sid;
  /* The running image always owns a group; no cached state across setpg/setsid. */
  if (__sys_getpg(0, &pgid, &sid))
    __builtin_trap();
  return pgid;
}
pid_t
getpgid(pid_t pid)
{
  return query(pid, 0);
}
pid_t
getsid(pid_t pid)
{
  return query(pid, 1);
}
int
setpgid(pid_t pid, pid_t pgid)
{
  isize error;
  if (pid < 0 || pgid < 0)
    return (int) failure(-ERR_INVALID, 0);
  /* A nonexistent destination group is EPERM, rather than a missing target
     process's ESRCH. Classification never grants access to that group. */
  if (pgid && !pidvalid(pgid))
    return (int) failure(-ERR_DENIED, 0);
  error = __sys_setpg(pid, pgid);
  return error ? (int) failure(error, 1) : 0;
}
pid_t
setsid(void)
{
  Handle sid;
  isize  error = __sys_setsid(&sid);
  return error ? failure(error, 0) : sid;
}
