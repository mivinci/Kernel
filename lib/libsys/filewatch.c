#include "private/filewatch.h"
#include "private/syscall.h"
#include <abi/observe.h>
#include <abi/time.h>

isize
sysopenprepare(Openprep *prep, const Fileowner *owner, Handle client, u64 image, u64 token,
               u64 session, u64 reference, u64 deadline)
{
  if (!prep || !owner || !owner->cookie || owner->reason || handlekind(client) != HANDLE_PROCESS ||
      !image || !token || !session || !reference || !deadline ||
      (owner->pid == client && owner->image == image))
    return -ERR_INVALID;
  if (prep->state != Openempty)
    return -ERR_BUSY;
  prep->preparer = owner->pid;
  prep->preparer_image = owner->image;
  prep->client = client;
  prep->client_image = image;
  prep->token = token;
  prep->session = session;
  prep->reference = reference;
  prep->deadline = deadline;
  prep->state = Openpending;
  return 0;
}

static isize
opencall(const Openprep *prep, const Ipcmsg *call, u64 opcode)
{
  if (!prep || !call || call->kind != IPC_CALL || call->words[0] != opcode || !call->words[2] ||
      !call->words[3])
    return -ERR_INVALID;
  return prep->state == Openempty || prep->token != call->words[3] ? -ERR_STALE : 0;
}

isize
sysopenattach(const Openprep *prep, const Ipcmsg *call, u64 now)
{
  isize status = opencall(prep, call, FS_ATTACH);
  if (status)
    return status;
  if (call->words[1])
    return -ERR_INVALID;
  if (call->source != prep->client || call->image != prep->client_image)
    return -ERR_DENIED;
  return prep->state != Openpending || sysopenexpired(prep, now) ? -ERR_STALE : 0;
}

void
sysopencommit(Openprep *prep)
{
  prep->deadline = 0;
  prep->state = Openattached;
}

isize
sysopenabort(const Openprep *prep, const Ipcmsg *call)
{
  isize status = opencall(prep, call, FS_ABORT);
  if (status)
    return status;
  if (!(call->source == prep->client && call->image == prep->client_image) &&
      !((prep->state == Openpending || prep->state == Opencancelled) &&
        call->source == prep->preparer && call->image == prep->preparer_image))
    return -ERR_DENIED;
  return prep->state == Openpending || prep->state == Openattached;
}

int
sysopenexpired(const Openprep *prep, u64 now)
{
  return prep->state == Openpending && now >= prep->deadline;
}

int
sysopenorphan(const Openprep *prep, const Fileowner *owner)
{
  return prep->state == Openpending && owner->cookie && owner->reason &&
         owner->pid == prep->preparer && owner->image == prep->preparer_image;
}

void
sysopenretire(Openprep *prep)
{
  if (prep->state == Openpending || prep->state == Openattached) {
    prep->state = prep->state == Openpending ? Opencancelled : Openclosed;
    prep->deadline = 0;
  }
}

isize
sysfilenow(u64 *now)
{
  int64_t seconds;
  u64     ns;
  isize   status;
  if (!now)
    return -ERR_INVALID;
  status = __sys_clock_gettime(CLOCK_MONOTONIC, &seconds, &ns);
  if (status)
    return status;
  if (seconds < 0 || ns >= UINT64_C(1000000000) ||
      (u64) seconds > (UINT64_MAX - ns) / UINT64_C(1000000000))
    return -ERR_OVERFLOW;
  *now = (u64) seconds * UINT64_C(1000000000) + ns;
  return 0;
}

isize
sysfileopendeadline(u64 *deadline)
{
  u64   now;
  isize status;
  if (!deadline)
    return -ERR_INVALID;
  status = sysfilenow(&now);
  if (status)
    return status;
  if (now > UINT64_MAX - SYSFILE_OPEN_TTL_NS)
    return -ERR_OVERFLOW;
  *deadline = now + SYSFILE_OPEN_TTL_NS;
  return 0;
}

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
