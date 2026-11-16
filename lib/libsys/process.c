#include "private/syscall.h"
#include <abi/syscall.h>
isize
__sys_create(Handle authority, const Createarg *arg, Handle *view, Handle *pid, Handle *control)
{
  const u64 args[7] = {(u64) authority, (u64) arg, 0, 0, 0, 0, 0};
  Sysret    result;
  isize     error;
  if (!view || !pid || !control)
    return -ERR_INVALID;
  error = __sys_enter(SYS_CREATE, args, &result);
  *view = (Handle) result.values[0];
  *pid = (Handle) result.values[1];
  *control = (Handle) result.values[2];
  return error;
}
isize
__sys_configure(Handle view, const Config *config, u64 reserve)
{
  const u64 args[7] = {(u64) view, (u64) config, reserve, 0, 0, 0, 0};
  Sysret    result;
  return __sys_enter(SYS_CONFIGURE, args, &result);
}
isize
__sys_start(Handle view)
{
  const u64 args[7] = {(u64) view, 0, 0, 0, 0, 0, 0};
  Sysret    result;
  return __sys_enter(SYS_START, args, &result);
}
isize
__sys_destroy(Handle view)
{
  const u64 args[7] = {(u64) view, 0, 0, 0, 0, 0, 0};
  Sysret    result;
  return __sys_enter(SYS_DESTROY, args, &result);
}
isize
__sys_getcred(Handle authority, Handle pid, u64 image, u32 ids[4])
{
  const u64 args[7] = {(u64) authority, (u64) pid, image, 0, 0, 0, 0};
  Sysret    result;
  isize     error;
  u32       i;
  if (!ids)
    return -ERR_INVALID;
  error = __sys_enter(SYS_GETCRED, args, &result);
  for (i = 0; i < 4; i++)
    ids[i] = (u32) result.values[i];
  return error;
}
