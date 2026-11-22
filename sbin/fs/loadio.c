#include "loadio.h"
#include <errno.h>

isize
loadiostart(Loadio *io, Loadtable *table, Fileinflight *active, const Ipcmsg *call,
            const Fileslot *slot, volatile u8 *page, u64 now)
{
  Loadfile file;
  u64      index, offset, bytes, dataoff, state, ticket;
  isize    error;
  if (!io || !table || !active || !call || !slot || !page || call->kind != IPC_CALL ||
      handlekind(call->request) != HANDLE_REQUEST || call->words[0] != FS_LOAD)
    return -ERR_INVALID;
  if (io->request)
    return -ERR_BUSY;
  index = call->words[3];
  if (index >= FS_CONN_SLOTS || active->requests[index] != call->request || !slot->seq ||
      slot->seq != call->words[2])
    return -ERR_STALE;
  offset = slot->args[4];
  bytes = slot->args[5];
  dataoff = slot->args[6];
  if (slot->span != 1 || !slot->object || slot->payload_bytes > FS_SLOT_BYTES || !bytes ||
      dataoff > slot->payload_bytes || bytes > slot->payload_bytes - dataoff || slot->args[7])
    return -ERR_INVALID;
  error = ipc_rqcheck(call->request, &state);
  if (error)
    return error;
  if (state == RQ_CHECK_DRAINING || state == (RQ_CHECK_LIVE | RQ_CHECK_CANCEL))
    return -ERR_CANCELLED;
  if (state != RQ_CHECK_LIVE)
    return -ERR_STATE;
  file.inode = slot->args[0];
  file.generation = slot->args[1];
  file.version = slot->args[2];
  file.length = slot->args[3];
  error =
      loadread(table, slot->object, call->source, call->image, &file, offset, bytes, now, &ticket);
  if (error)
    return error;
  io->file = file;
  io->table = table;
  io->active = active;
  io->page = page;
  io->request = call->request;
  io->session = slot->object;
  io->ticket = ticket;
  io->slot = index;
  io->offset = offset;
  io->bytes = bytes;
  io->dataoff = dataoff;
  io->reply.words[0] = io->reply.words[2] = io->reply.words[3] = 0;
  io->reply.words[1] = slot->seq;
  io->staged = 0;
  return 0;
}

isize
loadioreply(Loadio *io, const void *data, u64 bytes, u64 now)
{
  const u8 *source = data;
  u64       state, i;
  isize     error;
  if (!io || !io->request || !io->active || io->slot >= FS_CONN_SLOTS ||
      io->active->requests[io->slot] != io->request)
    return -ERR_STALE;
  error = ipc_rqcheck(io->request, &state);
  if (error)
    return error;
  if (state != RQ_CHECK_LIVE && state != (RQ_CHECK_LIVE | RQ_CHECK_CANCEL) &&
      state != RQ_CHECK_DRAINING)
    return -ERR_STATE;
  error = loadstatus(io->table, io->session, io->ticket, now);
  if (error == -ERR_STALE)
    return error;
  if (error || state != RQ_CHECK_LIVE) {
    io->reply.words[0] = (u64) (state & RQ_CHECK_CANCEL  ? -EINTR
                                : error == -ERR_TIMEDOUT ? -ETIMEDOUT
                                                         : -EIO);
    io->reply.words[2] = io->reply.words[3] = 0;
  } else if (!io->staged) {
    if (source && bytes == io->bytes) {
      for (i = 0; i < bytes; i++)
        io->page[io->dataoff + i] = source[i];
      io->reply.words[2] = io->reply.words[3] = bytes;
    } else
      io->reply.words[0] = (u64) -EIO;
  }
  io->staged = 1;
  if (state == RQ_CHECK_DRAINING)
    error = ipc_rqfinish(io->request);
  else
    error = ipc_reply(io->request, &io->reply);
  if (error)
    return error;
  /* The serialized owner cannot change the ticket during the native reply. */
  loadfinish(io->table, io->session, io->ticket, now);
  error = sysfilefinish(io->active, io->request);
  if (!error)
    io->request = 0;
  return error;
}

isize
loadcachebegin(Loadcache *work, Loadio *io, Cache *cache, const Loadspan *span)
{
  isize error;
  if (!work || !io || !io->request || !span || io->bytes > CACHE_BYTES ||
      io->offset % CACHE_BYTES != span->offset)
    return -ERR_INVALID;
  if (work->request)
    return -ERR_BUSY;
  error =
      cachespanbegin(cache, span->first, span->next, span->offset, (u32) io->bytes, &work->span);
  if (error)
    return error;
  work->blocks[0] = span->first;
  work->blocks[1] = span->next;
  work->next = work->failed = 0;
  work->request = io->request;
  return 0;
}

isize
loadcachecomplete(Loadcache *work, const Cacheio *ticket, u32 success)
{
  isize error;
  if (!work)
    return -ERR_STALE;
  error = cachefillfinish(&work->fill, ticket, success);
  if (!error) {
    work->failed = !success;
    work->next++;
  }
  return error;
}

isize
loadcachepump(Loadcache *work, Loadio *io, Loadfetch *out, void *scratch, u64 now)
{
  isize error;
  u64   state, bytes = 0;
  if (!work || !io || !out || !scratch || !work->request || work->request != io->request)
    return -ERR_INVALID;
  if (work->fill.cache)
    return -ERR_WOULD_BLOCK;
  if (work->span.cache) {
    error = ipc_rqcheck(io->request, &state);
    if (error)
      return error;
    if (!work->failed && state == RQ_CHECK_LIVE &&
        !loadstatus(io->table, io->session, io->ticket, now)) {
      while (work->next < ARRAY_SIZE(work->blocks)) {
        if (work->blocks[work->next]) {
          error = cachefillstart(&work->fill, work->span.cache, work->blocks[work->next],
                                 CACHE_DATA, &out->ticket, &out->destination);
          if (error == 1) {
            out->block = work->blocks[work->next];
            return 1;
          }
          if (error == -ERR_WOULD_BLOCK)
            return error;
          if (error) {
            work->failed = 1;
            break;
          }
        }
        work->next++;
      }
      if (!work->failed) {
        error = cachespancopy(&work->span, scratch);
        if (error)
          return error;
        bytes = io->bytes;
      }
    }
    error = cachespanend(&work->span);
    if (error)
      return error;
  }
  error = loadioreply(io, scratch, bytes, now);
  if (!error)
    work->request = 0;
  return error;
}
