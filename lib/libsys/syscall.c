#include "private/syscall.h"
#include <abi/syscall.h>
isize
__sys_observe(Handle permit, Handle basis, u64 relation, Handle pid, u64 image, u64 *cookie,
              u64 *state, u64 *reason)
{
  const u64 args[7] = {(u64) permit, (u64) basis, relation, (u64) pid, image, 0, 0};
  Sysret    result;
  isize     error;
  if (!cookie || !state || !reason)
    return -ERR_INVALID;
  error = __sys_enter(SYS_OBSERVE, args, &result);
  *cookie = result.values[0];
  *state = result.values[1];
  *reason = result.values[2];
  return error;
}
isize
__sys_obsread(u64 cookie, u64 *state, u64 *reason, Handle *pid, u64 *image)
{
  const u64 args[7] = {cookie, 0, 0, 0, 0, 0, 0};
  Sysret    result;
  isize     error;
  if (!state || !reason || !pid || !image)
    return -ERR_INVALID;
  error = __sys_enter(SYS_OBSREAD, args, &result);
  *state = result.values[0];
  *reason = result.values[1];
  *pid = (Handle) result.values[2];
  *image = result.values[3];
  return error;
}
isize
__sys_obsack(u64 cookie)
{
  const u64 args[7] = {cookie, 0, 0, 0, 0, 0, 0};
  Sysret    result;
  return __sys_enter(SYS_OBSACK, args, &result);
}
isize
__sys_obsdrop(u64 cookie)
{
  const u64 args[7] = {cookie, 0, 0, 0, 0, 0, 0};
  Sysret    result;
  return __sys_enter(SYS_OBSDROP, args, &result);
}
isize
__sys_clock_gettime(u64 clock, int64_t *seconds, u64 *nanoseconds)
{
  const u64 args[7] = {clock, 0, 0, 0, 0, 0, 0};
  Sysret    result;
  isize     error;
  if (!seconds || !nanoseconds)
    return -ERR_INVALID;
  error = __sys_enter(SYS_CLOCK_GETTIME, args, &result);
  *seconds = (int64_t) result.values[0];
  *nanoseconds = result.values[1];
  return error;
}
isize
__sys_alarmset(u64 deadline, u64 *generation)
{
  const u64 args[7] = {deadline, 0, 0, 0, 0, 0, 0};
  Sysret    result;
  isize     error;
  if (!generation)
    return -ERR_INVALID;
  error = __sys_enter(SYS_ALARMSET, args, &result);
  *generation = result.values[0];
  return error;
}
isize
__sys_alarmcancel(u64 *generation)
{
  const u64 args[7] = {0, 0, 0, 0, 0, 0, 0};
  Sysret    result;
  isize     error;
  if (!generation)
    return -ERR_INVALID;
  error = __sys_enter(SYS_ALARMCANCEL, args, &result);
  *generation = result.values[0];
  return error;
}
isize
__sys_debughandoff(Handle system)
{
  const u64 args[7] = {(u64) system, 0, 0, 0, 0, 0, 0};
  Sysret    result;
  return __sys_enter(SYS_DEBUGHANDOFF, args, &result);
}
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
