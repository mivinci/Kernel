#include "private/filewatch.h"
#include "private/syscall.h"
#include <abi/observe.h>

isize
sysfileowner(Fileowner *owner, Handle permit, const Ipcmsg *source)
{
  u64   cookie, state, reason;
  isize status;
  if (!owner || !source || source->kind != IPC_CALL ||
      handlekind(source->source) != HANDLE_PROCESS || !source->image ||
      handlekind(source->request) != HANDLE_REQUEST)
    return -ERR_INVALID;
  if (owner->cookie && (owner->pid != source->source || owner->image != source->image))
    return -ERR_BUSY;
  status = __sys_observe(permit, source->request, OBS_SOURCE, 0, 0, &cookie, &state, &reason);
  if (status)
    return status;
  if (state == OBS_ENDED) {
    if (owner->cookie)
      owner->reason = reason;
    return -ERR_PEER;
  }
  owner->pid = source->source;
  owner->image = source->image;
  owner->cookie = cookie;
  owner->reason = 0;
  return 0;
}

isize
sysfileownerread(Fileowner *owner)
{
  Handle pid;
  u64    image, state, reason;
  isize  status;
  if (!owner || !owner->cookie)
    return -ERR_INVALID;
  status = __sys_obsread(owner->cookie, &state, &reason, &pid, &image);
  if (status)
    return status;
  if (pid != owner->pid || image != owner->image)
    return -ERR_STALE;
  if (state == OBS_ENDED)
    owner->reason = reason;
  return 0;
}

isize
sysfileownerclose(Fileowner *owner)
{
  isize status;
  if (!owner || !owner->cookie)
    return -ERR_INVALID;
  status = owner->reason ? __sys_obsack(owner->cookie) : __sys_obsdrop(owner->cookie);
  if (!status) {
    owner->pid = 0;
    owner->image = owner->cookie = owner->reason = 0;
  }
  return status;
}

isize
sysfilewatch(Filewatch *watch, Handle permit, const Ipcmsg *source, u64 connid)
{
  isize status;
  if (!watch || !(connid >> 16) || (connid & 65535) >= FS_CONN_COUNT)
    return -ERR_INVALID;
  if (watch->owner.cookie)
    return -ERR_BUSY;
  status = sysfileowner(&watch->owner, permit, source);
  if (!status)
    watch->connid = connid;
  return status;
}

isize
sysfilewatchread(Filewatch *watch)
{
  return watch ? sysfileownerread(&watch->owner) : -ERR_INVALID;
}

isize
sysfilewatchaccept(Filewatch *watch, const volatile void *region, u64 bytes, const Ipcmsg *source,
                   Fileslot *out)
{
  if (!watch || !watch->owner.cookie || !source || source->kind != IPC_CALL)
    return -ERR_INVALID;
  if (source->source != watch->owner.pid || source->image != watch->owner.image ||
      source->words[1] != watch->connid)
    return -ERR_STALE;
  if (watch->owner.reason)
    return -ERR_PEER;
  return sysfileaccept(&watch->active, region, bytes, source->words[3], source->words[2],
                       source->request, out);
}

isize
sysfilewatchclose(Filewatch *watch)
{
  u32   i;
  isize status;
  if (!watch || !watch->owner.cookie)
    return -ERR_INVALID;
  for (i = 0; i < FS_CONN_SLOTS; i++)
    if (watch->active.requests[i])
      return -ERR_BUSY;
  status = sysfileownerclose(&watch->owner);
  if (!status)
    watch->connid = 0;
  return status;
}
