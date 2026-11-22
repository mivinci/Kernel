#include "pipedispatch.h"
#include "seek.h"
#include <errno.h>

static isize
fileerror(isize error)
{
  switch (error) {
  case -ERR_STALE:
    return -ESTALE;
  case -ERR_DENIED:
    return -EBADF;
  case -ERR_CAPACITY:
    return -ENFILE;
  case -ERR_OVERFLOW:
    return -EOVERFLOW;
  case -ERR_INVALID:
    return -EINVAL;
  case -ERR_BUSY:
    return -EBUSY;
  case -ERR_PEER:
    return -EIO;
  default:
    return error ? -EIO : 0;
  }
}

static isize
respond(Filelink *link, Handle request, const Msg *reply)
{
  isize error = ipc_reply(request, reply);
  return error || !link ? error : sysfilefinish(&link->watch.active, request);
}

static isize
advance(Pipewait *wait)
{
  isize error = pipeiostep(&wait->io, &wait->file->stream, &wait->link->watch.active);
  if (!error)
    error = pipedrop(&wait->file->stream, wait->io.end);
  return error;
}

static isize
pollstep(Pipetable *table, Pipepoll *poll, u32 waiting, isize status)
{
  Filelink          *link = poll->link;
  volatile Filepoll *page;
  Msg                reply = {{(u64) status, poll->seq, 0, 0}};
  u32                i, ready = 0;
  u64                state;
  isize              error;
  for (i = 0; !status && i < poll->count; i++) {
    Pipeready snapshot;
    Filepoll *row = &poll->rows[i];
    u64       events = row->events;
    if (!row->fd_generation || events & ~(u64) (FS_POLL_IN | FS_POLL_PRI | FS_POLL_OUT |
                                                FS_POLL_ERR | FS_POLL_HUP | FS_POLL_NVAL))
      status = -EINVAL;
    else {
      error = pipefileready(table, link->watch.owner.pid, link->watch.owner.image, row->session,
                            &snapshot);
      if (error)
        status = fileerror(error);
      else {
        row->revents = (snapshot.readable ? events & FS_POLL_IN : 0) |
                       (snapshot.room ? events & FS_POLL_OUT : 0) |
                       (snapshot.error ? FS_POLL_ERR : 0) | (snapshot.hup ? FS_POLL_HUP : 0);
        ready += !!row->revents;
      }
    }
  }
  error = ipc_rqcheck(poll->request, &state);
  if (error)
    return error;
  if (state == RQ_CHECK_DRAINING) {
    error = ipc_rqfinish(poll->request);
    if (!error)
      error = sysfilefinish(&link->watch.active, poll->request);
  } else {
    if (state & RQ_CHECK_CANCEL)
      status = -EINTR;
    if (!status) {
      if (waiting && !ready)
        return -ERR_WOULD_BLOCK;
      page = (volatile Filepoll *) (link->address + FS_DATA_OFFSET + poll->slot * FS_SLOT_BYTES);
      for (i = 0; i < poll->count; i++)
        page[i].revents = poll->rows[i].revents;
      reply.words[2] = ready;
      reply.words[3] = poll->count * sizeof(Filepoll);
    }
    reply.words[0] = (u64) status;
    error = respond(link, poll->request, &reply);
  }
  if (!error)
    poll->request = 0;
  return error;
}

static isize
pollstart(Pipedispatch *dispatch, Filelink *link, const Ipcmsg *call, const Fileslot *slot)
{
  Pipepoll draft;
  u32      i;
  isize    error;
  draft.request = call->request;
  draft.link = link;
  draft.seq = call->words[2];
  draft.slot = (u32) call->words[3];
  draft.count = 0;
  error = sysfilepollrows(&link->watch.active, draft.request, (const volatile void *) link->address,
                          FS_CONN_BYTES, draft.slot, slot, draft.rows, ARRAY_SIZE(draft.rows));
  if (!error)
    draft.count = (u32) slot->args[0];
  error = pollstep(dispatch->table, &draft, call->words[0] == FS_POLL_WAIT, fileerror(error));
  if (error != -ERR_WOULD_BLOCK)
    return error;
  for (i = 0; i < dispatch->npolls && dispatch->polls[i].request; i++) {
  }
  error = i == dispatch->npolls ? -ERR_BUSY : ipc_defer(draft.request);
  if (!error) {
    Pipepoll *poll = &dispatch->polls[i];
    poll->link = draft.link;
    poll->seq = draft.seq;
    poll->slot = draft.slot;
    poll->count = draft.count;
    for (i = 0; i < draft.count; i++)
      poll->rows[i] = draft.rows[i];
    poll->request = draft.request;
    return 0;
  }
  return pollstep(dispatch->table, &draft, 0, error == -ERR_BUSY ? -EAGAIN : fileerror(error));
}

isize
pipepump(Pipedispatch *dispatch)
{
  u32   i, progress;
  isize error;
  do {
    progress = 0;
    for (i = 0; i < dispatch->count; i++) {
      Pipewait *wait = &dispatch->waits[i];
      if (!wait->io.request)
        continue;
      error = advance(wait);
      if (error && error != -ERR_WOULD_BLOCK)
        return error;
      progress |= !error;
    }
  } while (progress);
  for (i = 0; i < dispatch->npolls; i++) {
    Pipepoll *poll = &dispatch->polls[i];
    if (!poll->request)
      continue;
    error = pollstep(dispatch->table, poll, 1, 0);
    if (error && error != -ERR_WOULD_BLOCK)
      return error;
  }
  return 0;
}

isize
pipedrain(void *context, Filelink *link)
{
  Pipedispatch *dispatch = context;
  u32           i;
  isize         error;
  if (dispatch->regular)
    fsfileretire(dispatch->regular, link->watch.owner.pid, link->watch.owner.image);
  error = dispatch->table
              ? pipefileretire(dispatch->table, link->watch.owner.pid, link->watch.owner.image)
              : 0;
  if (error)
    return error;
  for (i = 0; i < dispatch->npolls; i++) {
    Pipepoll *poll = &dispatch->polls[i];
    if (!poll->request || poll->link != link)
      continue;
    error = sysfilewatchdrain(&link->watch, poll->request);
    if (error)
      return error;
    poll->request = 0;
  }
  for (i = 0; i < dispatch->count; i++) {
    Pipewait *wait = &dispatch->waits[i];
    if (!wait->io.request || wait->link != link)
      continue;
    error = sysfilewatchdrain(&link->watch, wait->io.request);
    if (error)
      return error;
    wait->io.request = 0;
    error = pipedrop(&wait->file->stream, wait->io.end);
    if (error)
      return error;
  }
  return 0;
}

isize
pipedispatch(Pipedispatch *dispatch, Filelink *links, u32 count, const Ipcmsg *call)
{
  Fileslot     slot;
  Filelink    *link;
  volatile u8 *page;
  Pipewait     scratch;
  Pipewait    *wait;
  Pipekey      keys[2], key;
  u64          state, opcode = call->words[0];
  Msg          reply = {{0, call->words[2], 0, 0}};
  u32          end, i;
  isize        error;
  error = sysfilelinkaccept(links, count, call, &slot);
  if (error) {
    reply.words[0] = (u64) fileerror(error);
    return respond(NULL, call->request, &reply);
  }
  link = &links[call->words[1] & 65535];
  error = ipc_rqcheck(call->request, &state);
  if (error)
    return error;
  if (state == RQ_CHECK_DRAINING) {
    error = ipc_rqfinish(call->request);
    return error ? error : sysfilefinish(&link->watch.active, call->request);
  }
  if (state & RQ_CHECK_CANCEL && opcode != FS_CLOSE) {
    reply.words[0] = (u64) -EINTR;
    return respond(link, call->request, &reply);
  }
  page = (volatile u8 *) (link->address + FS_DATA_OFFSET + call->words[3] * FS_SLOT_BYTES);
  switch (opcode) {
  case FS_POLL_QUERY:
  case FS_POLL_WAIT:
    return pollstart(dispatch, link, call, &slot);
  case FS_PIPE: {
    Vcred cred;
    error = -ERR_INVALID;
    if (slot.object || slot.span != 1 || slot.payload_bytes || slot.args[0] ||
        slot.args[1] != PIPE_CAPACITY)
      break;
    for (i = 2; i < ARRAY_SIZE(slot.args) && !slot.args[i]; i++) {
    }
    if (i != ARRAY_SIZE(slot.args))
      break;
    if (!dispatch->table) {
      reply.words[0] = (u64) -ENOTSUP;
      return respond(link, call->request, &reply);
    }
    error = vcredsource(call, 0, &cred);
    if (error) {
      reply.words[0] = (u64) error;
      return respond(link, call->request, &reply);
    }
    error = pipefilecreate(dispatch->table, call->source, call->image, 0, cred, keys);
    if (!error) {
      for (i = 0; i < ARRAY_SIZE(keys); i++) {
        ((volatile u64 *) page)[2 * i] = keys[i].session;
        ((volatile u64 *) page)[2 * i + 1] = keys[i].reference;
      }
      reply.words[2] = 2;
      reply.words[3] = sizeof(keys);
    }
    break;
  }
  case FS_CLOSE:
  case FS_DUPREF:
    error = sysfileargs(opcode, &slot);
    if (error)
      break;
    key.session = slot.object;
    key.reference = slot.args[0];
    if (dispatch->regular && (key.session & FS_FILE_SESSION)) {
      if (opcode == FS_CLOSE)
        error =
            fsfileclose(dispatch->regular, call->source, call->image, key.session, key.reference);
      else
        error = fsfiledup(dispatch->regular, call->source, call->image, key.session, key.reference,
                          &reply.words[2]);
    } else if (!dispatch->table)
      error = -ERR_STALE;
    else if (opcode == FS_CLOSE)
      error = pipefileclose(dispatch->table, call->source, call->image, key);
    else
      error = pipefiledup(dispatch->table, call->source, call->image, key, &reply.words[2]);
    break;
  case FS_SYNC:
    error = sysfileargs(opcode, &slot);
    if (!error && dispatch->regular && (slot.object & FS_FILE_SESSION))
      return fssyncstart(dispatch->reads, dispatch->regular, link, call, &slot);
    if (!error)
      error = pipefilecheck(dispatch->table, call->source, call->image, slot.object);
    reply.words[0] = (u64) (error ? fileerror(error) : -EINVAL);
    return respond(link, call->request, &reply);
  case FS_SEEK:
    error = sysfileargs(opcode, &slot);
    if (!error && dispatch->regular && (slot.object & FS_FILE_SESSION)) {
      Fsfiles *table = dispatch->regular;
      Fsfile  *file = fsfilefind(table, call->source, call->image, slot.object);
      if (!file) {
        error = -ERR_STALE;
        break;
      }
      return fsseekstart(dispatch->reads, table, file, link, call, &slot);
    }
    if (!error)
      error = pipefilecheck(dispatch->table, call->source, call->image, slot.object);
    reply.words[0] = (u64) (error ? fileerror(error) : -ESPIPE);
    return respond(link, call->request, &reply);
  case FS_TTYCTL:
    if (slot.object & FS_FILE_SESSION)
      error =
          dispatch->regular && fsfilefind(dispatch->regular, call->source, call->image, slot.object)
              ? 0
              : -ERR_STALE;
    else
      error = pipefilecheck(dispatch->table, call->source, call->image, slot.object);
    reply.words[0] = (u64) (error ? fileerror(error) : -ENOTTY);
    return respond(link, call->request, &reply);
  case FS_STAT: {
    Fsfiles  *table = dispatch->regular;
    Filestat  stat = {0};
    const u8 *bytes = (const u8 *) &stat;
    error = sysfileargs(opcode, &slot);
    if (error)
      break;
    if (!(slot.object & FS_FILE_SESSION)) {
      error = pipefilestat(dispatch->table, call->source, call->image, slot.object, &stat);
      reply.words[0] = (u64) fileerror(error);
    } else if (!table || !fsfilefind(table, call->source, call->image, slot.object))
      reply.words[0] = (u64) -ESTALE;
    else
      return fsstatstart(dispatch->reads, table, link, call, &slot);
    if (!reply.words[0]) {
      stat.reserved = 0;
      for (i = 0; i < sizeof(stat); i++)
        page[i] = bytes[i];
      reply.words[3] = sizeof(stat);
    }
    return respond(link, call->request, &reply);
  }
  case FS_READ:
  case FS_WRITE:
    error = sysfileargs(opcode, &slot);
    if (error)
      break;
    if (dispatch->regular && (slot.object & FS_FILE_SESSION))
      return fsdatastart(dispatch->reads, dispatch->regular, link, call, &slot);
    end = opcode == FS_READ ? PIPE_READ : PIPE_WRITE;
    for (i = 0; i < dispatch->count && dispatch->waits[i].io.request; i++) {
    }
    /* A ready operation can unblock a parked peer even when all defer
       records are occupied. The scratch record never outlives dispatch. */
    scratch.io.request = 0;
    wait = i < dispatch->count ? &dispatch->waits[i] : &scratch;
    error = pipefilepin(dispatch->table, call->source, call->image, slot.object, end, &wait->file);
    if (error)
      break;
    wait->link = link;
    error = pipeiostart(&wait->io, &link->watch.active, call, &slot, page, end,
                        wait->file->nonblock[end]);
    if (error) {
      pipedrop(&wait->file->stream, end);
      break;
    }
    error = advance(wait);
    if (error != -ERR_WOULD_BLOCK)
      return error;
    error = wait == &scratch ? -ERR_BUSY : ipc_defer(call->request);
    if (error != -ERR_BUSY)
      return error;
    wait->io.fixed = 1;
    wait->io.reply.words[0] = (u64) -EAGAIN;
    return advance(wait);
  default:
    reply.words[0] = (u64) -ENOSYS;
    return respond(link, call->request, &reply);
  }
  reply.words[0] = (u64) fileerror(error);
  return respond(link, call->request, &reply);
}
