#include "private/filewatch.h"
#include "private/syscall.h"
#include <abi/observe.h>
#include <abi/time.h>
#include <errno.h>

isize
sysopenreserve(Openprep *pool, u32 count, Openlimit limit, const Fileowner *owner, Handle client,
               u64 image, u64 session, u64 reference, u64 deadline, u64 *token)
{
  Openprep *prep;
  u64       generation;
  u32       i, slot = count, clients = 0, preparers = 0;
  if (!pool || !count || count > SYSOPEN_POOL_MAX || !owner || !owner->cookie || owner->reason ||
      !limit.client || limit.client > count || !limit.preparer || limit.preparer > count ||
      !pidvalid(client) || !image || !token || !session || !reference ||
      !deadline || (owner->pid == client && owner->image == image))
    return -ERR_INVALID;
  for (i = 0; i < count; i++) {
    prep = &pool[i];
    if (prep->state == Openempty) {
      if (slot == count && (prep->token >> 16) != (UINT64_MAX >> 16))
        slot = i;
      continue;
    }
    if (prep->client == client && prep->client_image == image)
      clients++;
    if ((prep->state == Openpending || prep->state == Opencancelled) &&
        prep->preparer == owner->pid && prep->preparer_image == owner->image)
      preparers++;
  }
  if (slot == count || clients >= limit.client || preparers >= limit.preparer)
    return -ERR_CAPACITY;
  prep = &pool[slot];
  generation = (prep->token >> 16) + 1;
  prep->preparer = owner->pid;
  prep->preparer_image = owner->image;
  prep->client = client;
  prep->client_image = image;
  prep->token = (generation << 16) | slot;
  prep->session = session;
  prep->reference = reference;
  prep->deadline = deadline;
  prep->state = Openpending;
  *token = prep->token;
  return 0;
}

Openprep *
sysopenlookup(Openprep *pool, u32 count, u64 token)
{
  u32 i = (u32) (token & 65535);
  if (!pool || count > SYSOPEN_POOL_MAX || i >= count || !(token >> 16) ||
      pool[i].state == Openempty || pool[i].token != token)
    return NULL;
  return &pool[i];
}

isize
sysopenrelease(Openprep *pool, u32 count, u64 token)
{
  Openprep *prep = sysopenlookup(pool, count, token);
  if (!prep)
    return -ERR_STALE;
  if (prep->state != Openclosed && prep->state != Opencancelled)
    return -ERR_BUSY;
  prep->preparer = prep->client = 0;
  prep->preparer_image = prep->client_image = 0;
  prep->session = prep->reference = prep->deadline = 0;
  prep->state = Openempty;
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
sysopenreuse(Openprep *prep, const Filelink *link, const Ipcmsg *call, u64 now)
{
  Msg   reply = {{0, 0, 0, 0}};
  u64   state;
  isize status = sysopenattach(prep, call, now);
  if (status)
    return status;
  if (!link || !link->ready || !link->watch.connid || !link->watch.owner.cookie ||
      link->watch.owner.reason || link->watch.owner.pid != call->source ||
      link->watch.owner.image != call->image)
    return -ERR_STALE;
  status = ipc_rqcheck(call->request, &state);
  if (status)
    return status;
  if (state == RQ_CHECK_DRAINING)
    return ipc_rqfinish(call->request);
  reply.words[1] = call->words[2];
  if (state & RQ_CHECK_CANCEL) {
    reply.words[0] = (u64) -EINTR;
    return ipc_reply(call->request, &reply);
  }
  if (state != RQ_CHECK_LIVE)
    return -ERR_STATE;
  reply.words[2] = link->watch.connid;
  reply.words[3] = FS_CONN_BYTES;
  status = ipc_reply(call->request, &reply);
  if (!status)
    sysopencommit(prep);
  return status;
}

static Openprep *
openref(Openprep *pool, u32 count, Handle client, u64 image, u64 session, u64 reference, int closed)
{
  Openprep *found = NULL;
  u32       i;
  if (!pool || count > SYSOPEN_POOL_MAX)
    return NULL;
  for (i = 0; i < count; i++) {
    Openprep *prep = &pool[i];
    if ((prep->state == Openattached || (closed && prep->state == Openclosed)) &&
        prep->client == client && prep->client_image == image && prep->session == session &&
        prep->reference == reference) {
      if (found)
        return NULL;
      found = prep;
    }
  }
  return found;
}

Openprep *
sysopenref(Openprep *pool, u32 count, const Ipcmsg *call, const Fileslot *slot)
{
  if (!call || call->kind != IPC_CALL || !slot)
    return NULL;
  return openref(pool, count, call->source, call->image, slot->object, slot->args[0], 0);
}

isize
sysopenclose(Openprep *pool, u32 count, Handle client, u64 image, u64 session, u64 reference,
             Openrevoke revoke, void *context)
{
  Openprep *prep;
  isize     error;
  if (!pool || !count || count > SYSOPEN_POOL_MAX || !revoke ||
      !pidvalid(client) || !image || !session || !reference)
    return -ERR_INVALID;
  prep = openref(pool, count, client, image, session, reference, 1);
  if (!prep)
    return -ERR_STALE;
  if (prep->state == Openclosed)
    return 0;
  error = revoke(context, prep);
  if (!error)
    sysopenretire(prep);
  return error;
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

isize
sysopenabortreply(Openprep *pool, u32 count, const Filelink *link, const Ipcmsg *call,
                  Openrevoke revoke, void *context)
{
  Msg       reply = {{0, 0, 0, 0}};
  Openprep *prep;
  u64       state, connid;
  isize     error;
  if (!pool || !count || count > SYSOPEN_POOL_MAX || !revoke || !call || call->kind != IPC_CALL ||
      call->words[0] != FS_ABORT)
    return -ERR_INVALID;
  error = ipc_rqcheck(call->request, &state);
  if (error)
    return error;
  if (state == RQ_CHECK_DRAINING)
    return ipc_rqfinish(call->request);
  reply.words[1] = call->words[2];
  connid = call->words[1];
  if (state & RQ_CHECK_CANCEL)
    reply.words[0] = (u64) -EINTR;
  else if (state != RQ_CHECK_LIVE)
    return -ERR_STATE;
  else if (!call->words[2] || !call->words[3])
    reply.words[0] = (u64) -EINVAL;
  else if (connid &&
           (!link || !link->ready || link->watch.connid != connid || !link->watch.owner.cookie ||
            link->watch.owner.reason || link->watch.owner.pid != call->source ||
            link->watch.owner.image != call->image))
    reply.words[0] = (u64) -ESTALE;
  else {
    prep = sysopenlookup(pool, count, call->words[3]);
    error = prep ? sysopenabort(prep, call) : -ERR_STALE;
    if (error < 0)
      reply.words[0] = (u64) (error == -ERR_DENIED ? -EACCES : -ESTALE);
    else if (error == 1) {
      error = revoke(context, prep);
      if (error)
        return error;
      sysopenretire(prep);
    }
  }
  return ipc_reply(call->request, &reply);
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

u64
sysopennext(const Openprep *pool, u32 count)
{
  u64 next = 0;
  u32 i;
  if (!pool || count > SYSOPEN_POOL_MAX)
    return 0;
  for (i = 0; i < count; i++)
    if (pool[i].state == Openpending && (!next || pool[i].deadline < next))
      next = pool[i].deadline;
  return next;
}

isize
sysopensweep(Openprep *pool, u32 count, u64 now, const Fileowner *ended, Openrevoke revoke,
             void *context, u32 *retired)
{
  u32   i;
  isize status;
  if (!pool || !count || count > SYSOPEN_POOL_MAX || !revoke || !retired)
    return -ERR_INVALID;
  *retired = 0;
  for (i = 0; i < count; i++) {
    Openprep *prep = &pool[i];
    if (!sysopenexpired(prep, now) && !(ended && sysopenorphan(prep, ended)))
      continue;
    status = revoke(context, prep);
    if (status)
      return status;
    sysopenretire(prep);
    (*retired)++;
  }
  return 0;
}

isize
sysfilenow(u64 *now)
{
  int64_t seconds;
  u64     ns;
  isize   status;
  if (!now)
    return -ERR_INVALID;
  status = __sys_clock_gettime(ABI_CLOCK_MONOTONIC, &seconds, &ns);
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
      !pidvalid(source->source) || !source->image ||
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
sysopenclosereply(Openprep *pool, u32 count, Filewatch *watch, const Ipcmsg *call,
                  const Fileslot *slot, Openrevoke revoke, void *context)
{
  Msg       reply = {{0, 0, 0, 0}};
  Openprep *prep;
  u64       state;
  isize     error;
  if (!pool || !count || count > SYSOPEN_POOL_MAX || !revoke || !watch || !watch->owner.cookie ||
      !call || call->kind != IPC_CALL || call->words[0] != FS_CLOSE || !slot)
    return -ERR_INVALID;
  if (call->source != watch->owner.pid || call->image != watch->owner.image ||
      call->words[1] != watch->connid || call->words[2] != slot->seq ||
      call->words[3] >= FS_CONN_SLOTS || !call->request ||
      watch->active.requests[call->words[3]] != call->request)
    return -ERR_STALE;
  error = ipc_rqcheck(call->request, &state);
  if (error)
    return error;
  if (state == RQ_CHECK_DRAINING) {
    error = ipc_rqfinish(call->request);
    return error ? error : sysfilefinish(&watch->active, call->request);
  }
  if (state != RQ_CHECK_LIVE && state != (RQ_CHECK_LIVE | RQ_CHECK_CANCEL))
    return -ERR_STATE;
  reply.words[1] = slot->seq;
  if (sysfileargs(FS_CLOSE, slot))
    reply.words[0] = (u64) -EINVAL;
  else {
    prep = openref(pool, count, call->source, call->image, slot->object, slot->args[0], 1);
    if (!prep)
      reply.words[0] = (u64) -ESTALE;
    else if (prep->state == Openattached) {
      /* CLOSE is a cleanup obligation even after cooperative cancellation. */
      error = revoke(context, prep);
      if (error)
        return error;
      sysopenretire(prep);
    }
  }
  error = ipc_reply(call->request, &reply);
  return error ? error : sysfilefinish(&watch->active, call->request);
}

isize
sysfilewatchdrain(Filewatch *watch, Handle request)
{
  u32   i;
  u64   state;
  isize error;
  if (!watch || !watch->owner.cookie || handlekind(request) != HANDLE_REQUEST)
    return -ERR_INVALID;
  for (i = 0; i < FS_CONN_SLOTS && watch->active.requests[i] != request; i++) {
  }
  if (i == FS_CONN_SLOTS)
    return -ERR_STALE;
  if (!watch->owner.reason)
    return -ERR_BUSY;
  error = ipc_rqcheck(request, &state);
  if (error)
    return error;
  if (state != RQ_CHECK_DRAINING)
    return -ERR_WOULD_BLOCK;
  error = ipc_rqfinish(request);
  return error ? error : sysfilefinish(&watch->active, request);
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

isize
sysfilelinkclose(Filelink *link)
{
  u32   i;
  isize status;
  if (!link)
    return -ERR_INVALID;
  for (i = 0; i < FS_CONN_SLOTS; i++)
    if (link->watch.active.requests[i])
      return -ERR_BUSY;
  link->ready = link->prepaid = 0;
  if (link->address) {
    status = __sys_unmap(link->space, link->address);
    if (status)
      return status;
    link->address = 0;
    link->space = 0;
  }
  if (link->memory) {
    status = __sys_free(link->memory);
    if (status)
      return status;
    link->memory = 0;
  }
  status = link->watch.owner.cookie ? sysfilewatchclose(&link->watch) : 0;
  if (!status)
    link->serverimage = 0;
  return status;
}

static isize
linkallocate(Filelink *link, Handle allocator, Handle space, u64 address, u64 connid)
{
  Allocarg alloc = {FS_CONN_BYTES, ALLOC_SHARED, 0, 0, 0, 0};
  Maparg   map = {0, 0, address, FS_CONN_BYTES, MAP_READ | MAP_WRITE, 0};
  Filehead head = {FS_CONN_MAGIC, FS_CONN_BYTES, connid, 0, FS_CONN_SLOTS, FS_SLOT_BYTES, {0, 0}};
  Handle   self, parent;
  isize    status;
  status = __sys_getid(&self, &head.serverimage, &parent);
  if (status)
    return status;
  status = __sys_alloc(allocator, &alloc, &link->memory);
  if (!status) {
    map.mem = link->memory;
    status = __sys_map(space, &map);
  }
  if (!status) {
    link->space = space;
    link->address = address;
    status = __sys_copy((void *) (uintptr_t) address, &head, sizeof(head));
  }
  if (!status)
    link->serverimage = head.serverimage;
  return status;
}

static isize
linkhello(Filelink *link, Handle permit, Handle allocator, Handle space, u64 address, u64 connid,
          const Ipcmsg *call)
{
  Resreply reply = {0, MAP_READ | MAP_WRITE, 0, FS_CONN_BYTES, {0, connid, FS_CONN_BYTES}};
  isize    status = sysfilewatch(&link->watch, permit, call, connid);
  if (status)
    return status;
  status = linkallocate(link, allocator, space, address, connid);
  if (!status) {
    reply.source = link->memory;
    reply.business[0] = call->words[2];
    status = ipc_reply_resource(call->request, &reply);
  }
  if (!status)
    link->ready = 1;
  else
    (void) sysfilelinkclose(link);
  return status;
}

isize
sysfileprepay(Filelink *pool, u32 count, u32 index, Handle allocator, Handle space, u64 base)
{
  Filelink *link;
  u64       connid;
  u32       i;
  isize     status;
  if (!pool || count > FS_CONN_COUNT || index >= count || !base || (base & PAGE_MASK) ||
      (u64) count * FS_CONN_BYTES > UINT64_MAX - base)
    return -ERR_INVALID;
  link = &pool[index];
  if (!link->watch.owner.cookie || link->watch.owner.reason)
    return -ERR_STALE;
  if (link->ready || link->prepaid || link->memory || link->address || link->watch.connid)
    return -ERR_BUSY;
  for (i = 0; i < count; i++)
    if (i != index && pool[i].watch.owner.cookie &&
        pool[i].watch.owner.pid == link->watch.owner.pid &&
        pool[i].watch.owner.image == link->watch.owner.image)
      return -ERR_BUSY;
  if (link->generation >= (UINT64_MAX >> 16))
    return -ERR_CAPACITY;
  connid = (++link->generation << 16) | index;
  status = linkallocate(link, allocator, space, base + (u64) index * FS_CONN_BYTES, connid);
  if (!status) {
    link->watch.connid = connid;
    link->prepaid = 1;
  }
  return status;
}

static isize
linkconnect(Filelink *pool, u32 count, Handle permit, Handle allocator, Handle space, u64 base,
            const Ipcmsg *call)
{
  Filelink *link;
  u32       i, slot = count;
  if (!pool || !count || count > FS_CONN_COUNT || !base || (base & PAGE_MASK) ||
      (u64) count * FS_CONN_BYTES > UINT64_MAX - base)
    return -ERR_INVALID;
  for (i = 0; i < count; i++) {
    link = &pool[i];
    if (link->watch.owner.cookie && link->watch.owner.pid == call->source &&
        link->watch.owner.image == call->image)
      return -ERR_BUSY;
    if (slot == count && !link->ready && !link->memory && !link->address &&
        !link->watch.owner.cookie && link->generation < (UINT64_MAX >> 16))
      slot = i;
  }
  if (slot == count)
    return -ERR_CAPACITY;
  link = &pool[slot];
  link->generation++;
  return linkhello(link, permit, allocator, space, base + (u64) slot * FS_CONN_BYTES,
                   (link->generation << 16) | slot, call);
}

isize
sysopenfirst(Openprep *prep, Filelink *pool, u32 count, Handle permit, Handle allocator,
             Handle space, u64 base, const Ipcmsg *call, u64 now)
{
  Msg   reply = {{(u64) -EINTR, 0, 0, 0}};
  u64   state;
  isize status = sysopenattach(prep, call, now);
  if (status)
    return status;
  status = ipc_rqcheck(call->request, &state);
  if (status)
    return status;
  if (state == RQ_CHECK_DRAINING)
    return ipc_rqfinish(call->request);
  if (state & RQ_CHECK_CANCEL) {
    reply.words[1] = call->words[2];
    return ipc_reply(call->request, &reply);
  }
  if (state != RQ_CHECK_LIVE)
    return -ERR_STATE;
  status = linkconnect(pool, count, permit, allocator, space, base, call);
  if (!status)
    sysopencommit(prep);
  return status;
}

isize
sysslothello(Filelink *pool, u32 count, Handle permit, Handle allocator, Handle space, u64 base,
             u64 hello, const Ipcmsg *call)
{
  if (!hello || !call || call->kind != IPC_CALL || call->words[0] != hello || call->words[1] ||
      !call->words[2] || call->words[3] != UINT64_MAX)
    return -ERR_INVALID;
  return linkconnect(pool, count, permit, allocator, space, base, call);
}

isize
sysfilehello(Filelink *pool, u32 count, Handle permit, Handle allocator, Handle space, u64 base,
             const Ipcmsg *call)
{
  return sysslothello(pool, count, permit, allocator, space, base, FS_HELLO, call);
}

isize
sysfilelinkaccept(Filelink *pool, u32 count, const Ipcmsg *call, Fileslot *out)
{
  Filelink            *link;
  Filehead             head;
  const volatile void *region;
  u32                  slot;
  isize                status;
  if (!pool || count > FS_CONN_COUNT || !call || call->kind != IPC_CALL)
    return -ERR_INVALID;
  slot = (u32) (call->words[1] & 65535);
  if (slot >= count)
    return -ERR_STALE;
  link = &pool[slot];
  if (!link->ready || call->words[1] != link->watch.connid ||
      call->source != link->watch.owner.pid || call->image != link->watch.owner.image)
    return -ERR_STALE;
  if (link->watch.owner.reason)
    return -ERR_PEER;
  region = (const volatile void *) (uintptr_t) link->address;
  status = sysfilehead(region, FS_CONN_BYTES, link->watch.connid, link->serverimage, &head);
  if (status)
    return status;
  return sysfileaccept(&link->watch.active, region, FS_CONN_BYTES, call->words[3], call->words[2],
                       call->request, out);
}

isize
sysfilelinksweep(Filelink *pool, u32 count, Filedrain drain, void *context)
{
  isize first = 0, status;
  u32   i;
  if (!pool || !count || count > FS_CONN_COUNT || !drain)
    return -ERR_INVALID;
  for (i = 0; i < count; i++) {
    Filelink *link = &pool[i];
    if (link->ready || link->prepaid) {
      status = link->watch.owner.reason ? 0 : sysfilewatchread(&link->watch);
      if (!status && !link->watch.owner.reason)
        continue;
      if (!status)
        status = drain(context, link);
    } else {
      if (!link->address && !link->memory && !link->watch.owner.cookie)
        continue;
      status = 0;
    }
    if (!status)
      status = sysfilelinkclose(link);
    if (status && !first)
      first = status;
  }
  return first;
}
