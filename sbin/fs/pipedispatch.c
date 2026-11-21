#include "pipedispatch.h"
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
  return 0;
}

isize
pipedrain(void *context, Filelink *link)
{
  Pipedispatch *dispatch = context;
  u32           i;
  isize         error;
  error = pipefileretire(dispatch->table, link->watch.owner.pid, link->watch.owner.image);
  if (error)
    return error;
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
  case FS_PIPE:
    error = -ERR_INVALID;
    if (slot.object || slot.span != 1 || slot.payload_bytes || slot.args[0] ||
        slot.args[1] != PIPE_CAPACITY)
      break;
    for (i = 2; i < ARRAY_SIZE(slot.args) && !slot.args[i]; i++) {
    }
    if (i != ARRAY_SIZE(slot.args))
      break;
    error = pipefilecreate(dispatch->table, call->source, call->image, 0, keys);
    if (!error) {
      for (i = 0; i < ARRAY_SIZE(keys); i++) {
        ((volatile u64 *) page)[2 * i] = keys[i].session;
        ((volatile u64 *) page)[2 * i + 1] = keys[i].reference;
      }
      reply.words[2] = 2;
      reply.words[3] = sizeof(keys);
    }
    break;
  case FS_CLOSE:
  case FS_DUPREF:
    error = sysfileargs(opcode, &slot);
    if (error)
      break;
    key.session = slot.object;
    key.reference = slot.args[0];
    if (opcode == FS_CLOSE)
      error = pipefileclose(dispatch->table, call->source, call->image, key);
    else
      error = pipefiledup(dispatch->table, call->source, call->image, key, &reply.words[2]);
    break;
  case FS_READ:
  case FS_WRITE:
    error = sysfileargs(opcode, &slot);
    if (error)
      break;
    end = opcode == FS_READ ? PIPE_READ : PIPE_WRITE;
    for (i = 0; i < dispatch->count && dispatch->waits[i].io.request; i++) {
    }
    if (i == dispatch->count) {
      reply.words[0] = (u64) -EAGAIN;
      return respond(link, call->request, &reply);
    }
    wait = &dispatch->waits[i];
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
    error = ipc_defer(call->request);
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
