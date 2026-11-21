#include "private/filewatch.h"
#include "private/syscall.h"
#include <abi/observe.h>

isize
sysfilewatch(Filewatch *watch, Handle permit, const Ipcmsg *source, u64 connid)
{
  u64   cookie, state, reason;
  isize status;
  if (!watch || !source || source->kind != IPC_CALL ||
      handlekind(source->source) != HANDLE_PROCESS || !source->image ||
      handlekind(source->request) != HANDLE_REQUEST || !(connid >> 16) ||
      (connid & 65535) >= FS_CONN_COUNT)
    return -ERR_INVALID;
  if (watch->cookie)
    return -ERR_BUSY;
  status = __sys_observe(permit, source->request, OBS_SOURCE, 0, 0, &cookie, &state, &reason);
  if (status)
    return status;
  if (state == OBS_ENDED)
    return -ERR_PEER;
  watch->pid = source->source;
  watch->image = source->image;
  watch->connid = connid;
  watch->cookie = cookie;
  watch->reason = 0;
  return 0;
}

isize
sysfilewatchread(Filewatch *watch)
{
  Handle pid;
  u64    image, state, reason;
  isize  status;
  if (!watch || !watch->cookie)
    return -ERR_INVALID;
  status = __sys_obsread(watch->cookie, &state, &reason, &pid, &image);
  if (status)
    return status;
  if (pid != watch->pid || image != watch->image)
    return -ERR_STALE;
  if (state == OBS_ENDED)
    watch->reason = reason;
  return 0;
}

isize
sysfilewatchaccept(Filewatch *watch, const volatile void *region, u64 bytes, const Ipcmsg *source,
                   Fileslot *out)
{
  if (!watch || !watch->cookie || !source || source->kind != IPC_CALL)
    return -ERR_INVALID;
  if (source->source != watch->pid || source->image != watch->image ||
      source->words[1] != watch->connid)
    return -ERR_STALE;
  if (watch->reason)
    return -ERR_PEER;
  return sysfileaccept(&watch->active, region, bytes, source->words[3], source->words[2],
                       source->request, out);
}

isize
sysfilewatchclose(Filewatch *watch)
{
  u32   i;
  isize status;
  if (!watch || !watch->cookie)
    return -ERR_INVALID;
  for (i = 0; i < FS_CONN_SLOTS; i++)
    if (watch->active.requests[i])
      return -ERR_BUSY;
  status = watch->reason ? __sys_obsack(watch->cookie) : __sys_obsdrop(watch->cookie);
  if (!status) {
    watch->pid = 0;
    watch->image = watch->connid = watch->cookie = watch->reason = 0;
  }
  return status;
}
