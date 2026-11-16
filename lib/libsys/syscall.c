#include "private/syscall.h"
#include <abi/syscall.h>
NORETURN void
__sys_exit(u64 code)
{
  const u64 args[7] = {code, 0, 0, 0, 0, 0, 0};
  Sysret    result;
  __sys_enter(SYS_EXIT, args, &result);
  __builtin_trap();
}
isize
__sys_yield(void)
{
  const u64 args[7] = {0, 0, 0, 0, 0, 0, 0};
  Sysret    result;
  isize     status;
  status = __sys_enter(SYS_YIELD, args, &result);
  return status;
}
isize
__sys_getid(Handle *pid, u64 *image, Handle *parent)
{
  const u64 args[7] = {0, 0, 0, 0, 0, 0, 0};
  Sysret    result;
  isize     status;
  if (!pid || !image || !parent)
    return -ERR_INVALID;
  status = __sys_enter(SYS_GETID, args, &result);
  *pid = (Handle) result.values[0];
  *image = result.values[1];
  *parent = (Handle) result.values[2];
  return status;
}
isize
__sys_query(Handle authority, u64 kind, u64 cursor, Queryrec *records, u64 count, u64 *written,
            u64 *next)
{
  const u64 args[7] = {(u64) authority, kind, cursor, (u64) records, count, 0, 0};
  Sysret    result;
  isize     status;
  if (!written || !next)
    return -ERR_INVALID;
  status = __sys_enter(SYS_QUERY, args, &result);
  *written = result.values[0];
  *next = result.values[1];
  return status;
}
isize
__sys_wait(Handle selector, u64 options, Handle *pid, u64 *kind, u64 *code, u64 *sequence)
{
  const u64 args[7] = {(u64) selector, options, 0, 0, 0, 0, 0};
  Sysret    result;
  isize     status;
  if (!pid || !kind || !code || !sequence)
    return -ERR_INVALID;
  status = __sys_enter(SYS_WAIT, args, &result);
  *pid = (Handle) result.values[0];
  *kind = result.values[1];
  *code = result.values[2];
  *sequence = result.values[3];
  return status;
}
