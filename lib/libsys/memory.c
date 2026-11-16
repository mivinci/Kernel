#include "private/syscall.h"
#include <abi/syscall.h>
isize
__sys_alloc(Handle authority, const Allocarg *arg, Handle *out)
{
  const u64 args[7] = {(u64) authority, (u64) arg, 0, 0, 0, 0, 0};
  Sysret    result;
  isize     status;
  if (!out)
    return -ERR_INVALID;
  status = __sys_enter(SYS_ALLOC, args, &result);
  *out = (Handle) result.values[0];
  return status;
}
isize
__sys_free(Handle resource)
{
  const u64 args[7] = {(u64) resource, 0, 0, 0, 0, 0, 0};
  Sysret    result;
  isize     status;
  status = __sys_enter(SYS_FREE, args, &result);
  return status;
}
isize
__sys_map(Handle space, const Maparg *arg)
{
  const u64 args[7] = {(u64) space, (u64) arg, 0, 0, 0, 0, 0};
  Sysret    result;
  isize     status;
  status = __sys_enter(SYS_MAP, args, &result);
  return status;
}
isize
__sys_unmap(Handle space, u64 address)
{
  const u64 args[7] = {(u64) space, address, 0, 0, 0, 0, 0};
  Sysret    result;
  isize     status;
  status = __sys_enter(SYS_UNMAP, args, &result);
  return status;
}
isize
__sys_seal(Handle memory)
{
  const u64 args[7] = {(u64) memory, 0, 0, 0, 0, 0, 0};
  Sysret    result;
  isize     status;
  status = __sys_enter(SYS_SEAL, args, &result);
  return status;
}
