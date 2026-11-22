#include "transport.h"
#include <errno.h>
#include <proto/file.h>

enum
{
  BLKTRANSPORT_OPEN = 1,
  BLKTRANSPORT_FAILED
};

static isize
slotdone(Blktransport *transport, u32 link, u32 protocol_slot, u64 seq)
{
  volatile Protoslot *descriptor;
  u64                 offset;
  if (!transport || link >= transport->nlinks || protocol_slot >= PROTO_CONN_SLOTS ||
      !transport->links[link].address)
    return -ERR_INVALID;
  offset = PROTO_DESC_OFFSET + (u64) protocol_slot * PROTO_DESC_BYTES;
  if (offset > UINT64_MAX - transport->links[link].address)
    return -ERR_INVALID;
  descriptor = (volatile Protoslot *) (uintptr_t) (transport->links[link].address + offset);
  if (descriptor->seq != seq)
    return -ERR_STALE;
  if (descriptor->state != PROTO_SLOT_SUBMITTED)
    return -ERR_STALE;
  __atomic_store_n(&descriptor->state, PROTO_SLOT_DONE, __ATOMIC_RELEASE);
  return 0;
}

static isize
forward(Blktransport *transport, Blkjob *job)
{
  Msg   reply = {{job->status, job->seq, job->value, job->bytes}};
  u64   state;
  isize error;
  if (!job->replied) {
    error = ipc_rqcheck(job->request, &state);
    if (error)
      return error;
    if (state == RQ_CHECK_DRAINING)
      error = ipc_rqfinish(job->request);
    else
      error = ipc_reply(job->request, &reply);
    if (error)
      return error;
    job->replied = 1;
  }
  error = transport->filefinish(&transport->links[job->link].watch.active, job->request);
  if (!error)
    *job = (Blkjob){0};
  return error;
}

static isize
deliver(Blktransport *transport, Blkjob *job)
{
  isize error;
  if (!job->replied && !job->slotdone) {
    error = slotdone(transport, job->link, job->protocol_slot, job->seq);
    if (error) {
      job->status = (u64) -ESTALE;
      job->value = job->bytes = 0;
    } else
      job->slotdone = 1;
  }
  return forward(transport, job);
}

static isize
finishrequest(Blktransport *transport, Blkjob *job)
{
  isize error = deliver(transport, job);
  if (!error)
    transport->active--;
  return error;
}

static isize
completerequest(Blktransport *transport, u32 link, const Ipcmsg *call, isize status, u64 value,
                u64 bytes)
{
  Blkjob *job = &transport->immediate;
  job->request = call->request;
  job->seq = call->words[2];
  job->link = link;
  job->protocol_slot = (u32) call->words[3];
  job->status = (u64) status;
  job->value = value;
  job->bytes = bytes;
  return deliver(transport, job);
}

static isize
rejectadmitted(Blktransport *transport, u32 link, const Ipcmsg *call, isize status)
{
  return completerequest(transport, link, call, status, 0, 0);
}

static isize
rejectunadmitted(const Ipcmsg *call, isize status)
{
  Msg   reply = {{(u64) status, call->words[2], 0, 0}};
  u64   state;
  isize error = ipc_rqcheck(call->request, &state);
  if (error)
    return error;
  if (state == RQ_CHECK_DRAINING)
    return ipc_rqfinish(call->request);
  if (state & RQ_CHECK_CANCEL)
    reply.words[0] = (u64) -EINTR;
  return ipc_reply(call->request, &reply);
}

static u32
linkindex(const Blktransport *transport, const Ipcmsg *call)
{
  u32 index;
  if (!call)
    return transport->nlinks;
  index = (u32) (call->words[1] & 65535);
  return index < transport->nlinks ? index : transport->nlinks;
}

static void
zero(struct Blkinfo *info)
{
  u8 *bytes = (u8 *) info;
  u32 i;
  for (i = 0; i < sizeof(*info); i++)
    bytes[i] = 0;
}

static isize
writepage(const Filelink *link, u32 slot, const void *input, u32 bytes)
{
  volatile u8       *target;
  const volatile u8 *source = input;
  u64                offset;
  u32                i;
  if (!input || bytes > PROTO_SLOT_BYTES || !link->address || slot >= PROTO_CONN_SLOTS)
    return -ERR_INVALID;
  offset = PROTO_DATA_OFFSET + (u64) slot * PROTO_SLOT_BYTES;
  if (offset > UINT64_MAX - link->address)
    return -ERR_INVALID;
  target = (volatile u8 *) (uintptr_t) (link->address + offset);
  for (i = 0; i < bytes; i++)
    target[i] = source[i];
  return 0;
}

static isize
infostage(const Blktransport *transport, const Filelink *link, u32 protocol_slot)
{
  Blkinfo           info;
  const Vblkconfig *config = &transport->queue->config;
  zero(&info);
  info.magic = BLK_INFO_MAGIC;
  info.version = BLK_VERSION;
  info.device_generation = transport->device;
  info.geometry_generation = transport->queue->geometry;
  if (config->sectors > UINT64_MAX / UINT64_C(512))
    return -ERR_OVERFLOW;
  info.capacity_bytes = config->sectors * UINT64_C(512);
  info.logical_bytes = config->blocksize;
  info.physical_bytes = config->physical;
  info.alignment_bytes = config->alignment;
  info.flags = (config->readonly ? BLK_READONLY : 0) | (config->flush ? BLK_FLUSHABLE : 0);
  return writepage(link, protocol_slot, &info, sizeof(info));
}

static isize
submit(Blktransport *transport, const Ipcmsg *call, Filelink *link, const Fileslot *slot, u32 index)
{
  const u8 *payload;
  Blkjob   *job;
  u64       offset, state;
  u32       type, queue_slot, linkslot = (u32) (call->words[1] & 65535);
  isize     error;
  if (!transport || !call || !link || !slot || index >= PROTO_CONN_SLOTS)
    return -ERR_INVALID;
  type = call->words[0] == BLK_WORD(BLK_READ)    ? VBLK_READ
         : call->words[0] == BLK_WORD(BLK_WRITE) ? VBLK_WRITE
                                                 : VBLK_FLUSH;
  if (type == VBLK_WRITE) {
    offset = PROTO_DATA_OFFSET + (u64) index * PROTO_SLOT_BYTES;
    if (!link->address || offset > UINT64_MAX - link->address)
      return rejectadmitted(transport, linkslot, call, -EINVAL);
    payload = (const u8 *) (uintptr_t) (link->address + offset);
  } else
    payload = NULL;
  error = ipc_rqcheck(call->request, &state);
  if (error)
    return error;
  if (state != RQ_CHECK_LIVE)
    return completerequest(transport, linkslot, call, -EINTR, 0, 0);
  if (transport->queue->state != BLKQ_OPEN)
    return completerequest(transport, linkslot, call, -EIO, 0, 0);
  if (slot->args[2] != transport->queue->geometry)
    return completerequest(transport, linkslot, call, -ESTALE, 0, 0);
  if (transport->queue->configpending)
    return completerequest(transport, linkslot, call, -EAGAIN, 0, 0);
  for (queue_slot = 0; queue_slot < transport->njobs && (transport->jobs[queue_slot].request ||
                                                         transport->queue->slots[queue_slot].held);
       queue_slot++) {
  }
  if (queue_slot == transport->njobs)
    return rejectadmitted(transport, linkslot, call, -EAGAIN);
  job = &transport->jobs[queue_slot];
  job->request = call->request;
  job->seq = slot->seq;
  job->offset = slot->args[0];
  job->length = (u32) slot->args[1];
  job->type = type;
  job->protocol_slot = index;
  job->queue_slot = queue_slot;
  job->link = linkslot;
  error = ipc_defer(call->request);
  if (!error) {
    /* The block wire is byte-oriented; the current sector-backed transport
       rejects an offset that cannot be represented without splitting. */
    if (job->offset % VBLK_SECTOR || (job->type != VBLK_FLUSH && !job->length))
      error = -ERR_INVALID;
    else
      error = blkqsubmitbytes(transport->queue, queue_slot, type, job->offset, job->length, payload,
                              transport->sync, transport->syncctx);
    if (!error) {
      transport->active++;
      return 0;
    }
    if (!transport->queue->slots[queue_slot].active && !transport->queue->slots[queue_slot].held) {
      *job = (Blkjob){0};
      return rejectadmitted(transport, linkslot, call,
                            error == -ERR_INVALID       ? -EINVAL
                            : error == -ERR_NOTSUP      ? -ENOTSUP
                            : error == -ERR_WOULD_BLOCK ? -EAGAIN
                                                        : -EIO);
    }
    transport->active++;
    transport->state = BLKTRANSPORT_FAILED;
    transport->error = (u64) -EIO;
    return error;
  }
  if (transport->queue->slots[queue_slot].active || transport->queue->slots[queue_slot].held) {
    transport->state = BLKTRANSPORT_FAILED;
    transport->error = (u64) -EIO;
    return error;
  }
  *job = (Blkjob){0};
  if (transport->queue->state != BLKQ_OPEN)
    transport->state = BLKTRANSPORT_FAILED;
  /*
   * Do not swallow a successful defer if local job storage was inconsistent.
   * blkqsubmitbytes validates its own inputs but every local request slot is
   * known to have been checked above.
   */
  if (error == -ERR_BUSY || error == -ERR_WOULD_BLOCK)
    return rejectadmitted(transport, linkslot, call, -EAGAIN);
  if (error == -ERR_INVALID)
    return rejectadmitted(transport, linkslot, call, -EINVAL);
  return rejectadmitted(transport, linkslot, call, -EIO);
}

isize
blktransportinit(Blktransport *transport, Blkqueue *queue, Filelink *links, u32 nlinks,
                 Blkjob *jobs, u32 njobs, u64 device, Vblksync sync, void *syncctx)
{
  u32 i;
  if (!transport || !queue || !links || !nlinks || nlinks > FS_CONN_COUNT || !jobs || !njobs ||
      njobs > queue->count || njobs > VQ_SIZE || !device || !sync)
    return -ERR_INVALID;
  for (i = 0; i < njobs; i++)
    jobs[i] = (Blkjob){0};
  transport->queue = queue;
  transport->links = links;
  transport->jobs = jobs;
  transport->immediate = (Blkjob){0};
  transport->nlinks = nlinks;
  transport->njobs = njobs;
  transport->reaper = 0;
  transport->drain = 0;
  transport->filefinish = sysfilefinish;
  transport->device = device;
  transport->sync = sync;
  transport->syncctx = syncctx;
  transport->error = 0;
  transport->state = BLKTRANSPORT_OPEN;
  transport->active = 0;
  return 0;
}

void
blktransportfilefinish(Blktransport *transport, Blkfilefinish finish)
{
  if (transport && finish)
    transport->filefinish = finish;
}

isize
blktransportreaper(Blktransport *transport, void *context, Blkdrainlink drain)
{
  if (!transport || !context || !drain)
    return -ERR_INVALID;
  transport->reaper = context;
  transport->drain = drain;
  return 0;
}

isize
blktransportcall(Blktransport *transport, const Ipcmsg *call)
{
  Filelink *link;
  Fileslot  slot;
  u64       state;
  u32       index, opcode, linkslot;
  isize     error;
  if (!transport || !call || call->kind != IPC_CALL ||
      BLK_WORD_VERSION(call->words[0]) != BLK_VERSION || call->words[3] >= PROTO_CONN_SLOTS)
    return call ? rejectunadmitted(call, -EINVAL) : -ERR_INVALID;
  if (transport->immediate.request && deliver(transport, &transport->immediate))
    return rejectunadmitted(call, -EAGAIN);
  index = linkindex(transport, call);
  if (index == transport->nlinks)
    return rejectunadmitted(call, -ERR_STALE);
  link = &transport->links[index];
  linkslot = index;
  if (!blktransportonline(transport))
    return rejectunadmitted(call, -EIO);
  error = sysfilelinkaccept(transport->links, transport->nlinks, call, &slot);
  if (error)
    return rejectunadmitted(call, error == -ERR_STALE ? -ERR_STALE : -EINVAL);
  if (!link->address)
    return rejectadmitted(transport, linkslot, call, -EINVAL);
  index = (u32) call->words[3];
  if (slot.span != 1 || slot.object || slot.reserved[0] || slot.reserved[1] || slot.reserved[2] ||
      slot.payload_bytes > PROTO_SLOT_BYTES)
    return rejectadmitted(transport, linkslot, call, -EINVAL);
  opcode = BLK_WORD_OPCODE(call->words[0]);
  switch (opcode) {
  case BLK_INFO:
    if (slot.payload_bytes != BLK_INFO_BYTES || slot.args[0] || slot.args[1] || slot.args[2] ||
        slot.args[3] || slot.args[4] || slot.args[5] || slot.args[6] || slot.args[7])
      return rejectadmitted(transport, linkslot, call, -EINVAL);
    error = ipc_rqcheck(call->request, &state);
    if (error)
      return error;
    if (state != RQ_CHECK_LIVE)
      return completerequest(transport, linkslot, call, -EINTR, 0, 0);
    error = infostage(transport, link, index);
    return error ? rejectadmitted(transport, linkslot, call, -EIO)
                 : completerequest(transport, linkslot, call, 0, transport->queue->geometry,
                                   BLK_INFO_BYTES);
  case BLK_READ:
  case BLK_WRITE:
    if (slot.payload_bytes != slot.args[1] || !slot.args[1] || slot.args[1] > BLK_MAX_IO ||
        slot.args[3] || slot.args[4] || slot.args[5] || slot.args[6] || slot.args[7])
      return rejectadmitted(transport, linkslot, call, -EINVAL);
    return submit(transport, call, link, &slot, index);
  case BLK_FLUSH:
    if (slot.payload_bytes || slot.args[0] || slot.args[1] || slot.args[3] || slot.args[4] ||
        slot.args[5] || slot.args[6] || slot.args[7])
      return rejectadmitted(transport, linkslot, call, -EINVAL);
    return submit(transport, call, link, &slot, index);
  default:
    return rejectadmitted(transport, linkslot, call, -ENOSYS);
  }
}

static isize
complete(Blktransport *transport, Blkjob *job)
{
  Filelink *link;
  u8       *page;
  u64       offset, state;
  int       result;
  isize     error;
  link = &transport->links[job->link];
  error = ipc_rqcheck(job->request, &state);
  if (error)
    return error;
  offset = PROTO_DATA_OFFSET + (u64) job->protocol_slot * PROTO_SLOT_BYTES;
  if (job->type == VBLK_READ && state == RQ_CHECK_LIVE &&
      (!link->address || offset > UINT64_MAX - link->address))
    return -ERR_STATE;
  page = job->type == VBLK_READ && state == RQ_CHECK_LIVE && link->address
             ? (u8 *) (uintptr_t) (link->address + offset)
             : NULL;
  result = blkqfinish(transport->queue, &job->done,
                      job->type == VBLK_READ && state == RQ_CHECK_LIVE ? page : NULL,
                      job->type == VBLK_READ && state == RQ_CHECK_LIVE ? job->length : 0);
  if (result < 0) {
    if (result == -ERR_PEER)
      transport->state = BLKTRANSPORT_FAILED;
    return result;
  }
  if (transport->queue->state != BLKQ_OPEN)
    transport->state = BLKTRANSPORT_FAILED;
  if (result == VBLK_OK && job->type == VBLK_READ && state != RQ_CHECK_LIVE)
    job->status = (u64) -EINTR;
  else if (result == VBLK_OK) {
    job->status = 0;
    if (job->type == VBLK_READ || job->type == VBLK_WRITE)
      job->value = job->bytes = job->length;
  } else if (result == VBLK_IOERR)
    job->status = (u64) -EIO;
  else
    job->status = (u64) -ENOTSUP;
  job->pending = 0;
  job->completed = 1;
  error = finishrequest(transport, job);
  return error && transport->state == BLKTRANSPORT_FAILED ? error : 0;
}

static isize
drain(Blktransport *transport, int retry)
{
  u32   i;
  isize first = 0, error;
  for (i = 0; i < transport->njobs; i++) {
    Blkjob *job = &transport->jobs[i];
    if (job->pending)
      error = complete(transport, job);
    else if (retry && job->completed)
      error = finishrequest(transport, job);
    else
      continue;
    if (error && !first)
      first = error;
  }
  return first;
}

isize
blktransportpump(Blktransport *transport)
{
  isize first = 0, error;
  if (!transport)
    return -ERR_INVALID;
  if (transport->immediate.request)
    first = deliver(transport, &transport->immediate);
  error = drain(transport, 1);
  return first ? first : error;
}

isize
blktransportabort(Blktransport *transport, Blkjob *job, isize status)
{
  u64   state;
  isize error;
  if (!transport || !job || !job->request || job->link >= transport->nlinks ||
      job->queue_slot >= transport->njobs || !transport->jobs[job->queue_slot].request ||
      &transport->jobs[job->queue_slot] != job)
    return -ERR_INVALID;
  if (transport->queue->slots[job->queue_slot].active ||
      transport->queue->slots[job->queue_slot].held)
    return -ERR_BUSY;
  error = ipc_rqcheck(job->request, &state);
  if (error)
    return error;
  if (job->completed)
    return finishrequest(transport, job);
  job->status = (u64) status;
  job->value = job->bytes = 0;
  job->pending = 0;
  job->completed = 1;
  return finishrequest(transport, job);
}

isize
blktransportpoll(Blktransport *transport, Vdone *done, u32 *count, u32 *more, u32 *causes)
{
  u32   n, i;
  isize error;
  if (!transport || !done || !count || !more || !causes || !transport->njobs)
    return -ERR_INVALID;
  *count = *more = *causes = 0;
  error = blktransportpump(transport);
  if (error)
    return error;
  error = blkqpoll(transport->queue, done, &n, more, causes);
  if (error) {
    transport->state = BLKTRANSPORT_FAILED;
    return error;
  }
  if (n > transport->njobs) {
    transport->state = BLKTRANSPORT_FAILED;
    return -ERR_STATE;
  }
  for (i = 0; i < n; i++) {
    int index = blkqmatch(transport->queue, &done[i]);
    if (index < 0 || (u32) index >= transport->njobs) {
      transport->state = BLKTRANSPORT_FAILED;
      return index < 0 ? index : -ERR_STATE;
    }
    if (!transport->jobs[index].request || transport->jobs[index].pending ||
        transport->jobs[index].queue_slot != (u32) index ||
        transport->jobs[index].link >= transport->nlinks ||
        transport->jobs[index].protocol_slot >= PROTO_CONN_SLOTS) {
      transport->state = BLKTRANSPORT_FAILED;
      return -ERR_STATE;
    }
  }
  /* The used ring has advanced. Keep every result before checking any IPC
     request so a retry can never lose this or a later completion. */
  for (i = 0; i < n; i++) {
    int index = blkqmatch(transport->queue, &done[i]);
    transport->jobs[index].done = done[i];
    transport->jobs[index].pending = 1;
  }
  error = drain(transport, 0);
  if (error)
    return error;
  *count = n;
  return 0;
}

isize
blktransportfail(Blktransport *transport, isize status)
{
  u32   i;
  isize error, first = 0;
  if (!transport || status >= 0)
    return -ERR_INVALID;
  transport->state = BLKTRANSPORT_FAILED;
  transport->error = (u64) status;
  if (transport->immediate.request)
    first = deliver(transport, &transport->immediate);
  for (i = 0; i < transport->njobs; i++) {
    Blkjob *job = &transport->jobs[i];
    if (!job->request || !job->completed)
      continue;
    error = finishrequest(transport, job);
    if (error && !first)
      first = error;
  }
  for (i = 0; i < transport->njobs; i++) {
    Blkjob *job = &transport->jobs[i];
    if (!job->request || job->completed || transport->queue->slots[i].active ||
        transport->queue->slots[i].held)
      continue;
    error = blktransportabort(transport, job, status);
    if (error && !first)
      first = error;
  }
  return first;
}

isize
blktransportrecover(Blktransport *transport, u64 now)
{
  isize error;
  u32   i;
  if (!transport || !transport->queue)
    return -ERR_INVALID;
  transport->state = BLKTRANSPORT_FAILED;
  error = blkqrecover(transport->queue, now);
  if (error)
    return error;
  if (transport->immediate.request) {
    error = deliver(transport, &transport->immediate);
    if (error)
      return error;
  }
  for (i = 0; i < transport->njobs; i++) {
    Blkjob *job = &transport->jobs[i];
    isize   status;
    if (!job->request || !job->completed)
      continue;
    status = finishrequest(transport, job);
    if (status)
      return status;
  }
  for (i = 0; i < transport->njobs; i++) {
    Blkjob *job = &transport->jobs[i];
    isize   status;
    if (!job->request)
      continue;
    status = blktransportabort(transport, job, transport->error ? (isize) transport->error : -EIO);
    if (status)
      return status;
  }
  return transport->active ? -ERR_STATE : 0;
}

isize
blktransportreap(Blktransport *transport)
{
  if (!transport || !transport->links || !transport->drain || !transport->reaper)
    return -ERR_INVALID;
  return sysfilelinksweep(transport->links, transport->nlinks, transport->drain, transport->reaper);
}

int
blktransportonline(const Blktransport *transport)
{
  return transport->state == BLKTRANSPORT_OPEN && transport->queue->state == BLKQ_OPEN;
}

u64
blktransportgeneration(const Blktransport *transport)
{
  return transport && transport->queue ? transport->queue->generation : 0;
}

int
blktransportbusy(const Blktransport *transport, const Filelink *link)
{
  u32 i;
  if (!transport || !link || !transport->jobs || !transport->links)
    return 0;
  if (transport->immediate.request && &transport->links[transport->immediate.link] == link)
    return 1;
  for (i = 0; i < transport->njobs; i++)
    if (transport->jobs[i].request && transport->jobs[i].link < transport->nlinks &&
        &transport->links[transport->jobs[i].link] == link)
      return 1;
  return 0;
}
