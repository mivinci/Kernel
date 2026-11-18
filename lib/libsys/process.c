#include "private/syscall.h"
#include <abi/syscall.h>
isize
__sys_inherit(Handle resource, u64 flags)
{
  const u64 args[7] = {(u64) resource, flags, 0, 0, 0, 0, 0};
  Sysret    result;
  return __sys_enter(SYS_INHERIT, args, &result);
}
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

isize
__sys_connect(Handle authority, u64 kind, u64 rights, u64 bits, Handle *out)
{
  const u64 args[7] = {(u64) authority, kind, rights, bits, 0, 0, 0};
  Sysret    result;
  isize     error;
  if (!out)
    return -ERR_INVALID;
  error = __sys_enter(SYS_CONNECT, args, &result);
  *out = (Handle) result.values[0];
  return error;
}
isize
__sys_grant(Handle target, Handle source, u64 rights, u64 offset, u64 bytes, u64 inherit,
            Handle *out)
{
  const u64 args[7] = {(u64) target, (u64) source, rights, offset, bytes, inherit, 0};
  Sysret    result;
  isize     error;
  if (!out)
    return -ERR_INVALID;
  error = __sys_enter(SYS_GRANT, args, &result);
  *out = (Handle) result.values[0];
  return error;
}

isize
__sys_terminate(Handle control)
{
  const u64 args[7] = {(u64) control, 0, 0, 0, 0, 0, 0};
  Sysret    result;
  return __sys_enter(SYS_TERMINATE, args, &result);
}

isize
__sys_revoke(Handle target, Handle resource)
{
  const u64 args[7] = {(u64) target, (u64) resource, 0, 0, 0, 0, 0};
  Sysret    result;
  return __sys_enter(SYS_REVOKE, args, &result);
}

isize
__sys_getpg(Handle pid, Handle *pgid, Handle *sid)
{
  const u64 args[7] = {(u64) pid, 0, 0, 0, 0, 0, 0};
  Sysret    result;
  isize     error;
  if (!pgid || !sid)
    return -ERR_INVALID;
  error = __sys_enter(SYS_GETPG, args, &result);
  *pgid = (Handle) result.values[0];
  *sid = (Handle) result.values[1];
  return error;
}
isize
__sys_setpg(Handle pid, Handle pgid)
{
  const u64 args[7] = {(u64) pid, (u64) pgid, 0, 0, 0, 0, 0};
  Sysret    result;
  return __sys_enter(SYS_SETPG, args, &result);
}
isize
__sys_setsid(Handle *sid)
{
  const u64 args[7] = {0, 0, 0, 0, 0, 0, 0};
  Sysret    result;
  isize     error;
  if (!sid)
    return -ERR_INVALID;
  error = __sys_enter(SYS_SETSID, args, &result);
  *sid = (Handle) result.values[0];
  return error;
}

isize
__sys_irqbind(Handle target, Handle source, u32 bit, Handle *binding)
{
  const u64 args[7] = {(u64) target, (u64) source, bit, 0, 0, 0, 0};
  Sysret    result;
  isize     error;
  if (!binding)
    return -ERR_INVALID;
  error = __sys_enter(SYS_IRQBIND, args, &result);
  *binding = (Handle) result.values[0];
  return error;
}
isize
__sys_irqack(Handle binding)
{
  const u64 args[7] = {(u64) binding, 0, 0, 0, 0, 0, 0};
  Sysret    result;
  return __sys_enter(SYS_IRQACK, args, &result);
}
isize
__sys_irqunbind(Handle target, Handle binding)
{
  const u64 args[7] = {(u64) target, (u64) binding, 0, 0, 0, 0, 0};
  Sysret    result;
  return __sys_enter(SYS_IRQUNBIND, args, &result);
}
