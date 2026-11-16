#include "private/syscall.h"
#include <abi/syscall.h>
isize
__sys_sigaction(u64 signum, const Sigact *action, Sigact *old)
{
  const u64 args[7] = {signum, (u64) action, (u64) old, 0, 0, 0, 0};
  Sysret    result;
  return __sys_enter(SYS_SIGACTION, args, &result);
}
isize
__sys_sigmask(u64 how, u64 mask, u64 *old)
{
  const u64 args[7] = {how, mask, 0, 0, 0, 0, 0};
  Sysret    result;
  isize     error;
  if (!old)
    return -ERR_INVALID;
  error = __sys_enter(SYS_SIGMASK, args, &result);
  *old = result.values[0];
  return error;
}
isize
__sys_sigpending(u64 *out)
{
  const u64 args[7] = {0, 0, 0, 0, 0, 0, 0};
  Sysret    result;
  isize     error;
  if (!out)
    return -ERR_INVALID;
  error = __sys_enter(SYS_SIGPENDING, args, &result);
  *out = result.values[0];
  return error;
}
