#include "read.h"
#include <errno.h>

isize
filereadcomplete(Readwork *work, const Cacheio *ticket, const void *block, u32 bytes)
{
  isize error;
  if (!work)
    return -ERR_STALE;
  if (!block && bytes)
    return -ERR_INVALID;
  error = block ? cachefillcopy(&work->fill, ticket, block, bytes)
                : cachefillfinish(&work->fill, ticket, 0);
  if (!error)
    work->failed = !block;
  return error;
}

isize
filedata(Readwork *work, Readfetch *fetch, Fileinflight *active, const Ipcmsg *call,
         const Fileslot *slot, const Loadmap *map, const Loadindex *index, Cache *cache, u32 first,
         u64 *position, void *scratch, volatile u8 *page, Loadtable *loads)
{
  Msg       reply = {{0, 0, 0, 0}};
  Loadspan  span;
  u64       state, at, offset, bytes = 0, i;
  const u8 *data = scratch;
  u32       positional, writing, blocks[2];
  isize     error;
  if (!work || !fetch || !active || !call || !slot || !map || !cache ||
      cache->blocks > UINT32_MAX || !position || !scratch || !page || call->kind != IPC_CALL ||
      (call->words[0] != FS_READ && call->words[0] != FS_WRITE) ||
      handlekind(call->request) != HANDLE_REQUEST)
    return -ERR_INVALID;
  at = call->words[3];
  if (at >= FS_CONN_SLOTS || active->requests[at] != call->request || !slot->seq ||
      slot->seq != call->words[2])
    return -ERR_STALE;
  if (work->request &&
      (work->request != call->request || (work->span.cache && work->span.cache != cache)))
    return -ERR_BUSY;
  if (work->fill.cache)
    return -ERR_WOULD_BLOCK;
  error = ipc_rqcheck(call->request, &state);
  if (error)
    return error;
  if (state == RQ_CHECK_DRAINING) {
    if (work->span.cache && (error = cachespanend(&work->span)))
      return error;
    error = ipc_rqfinish(call->request);
    goto finished;
  }
  if (state != RQ_CHECK_LIVE && state != (RQ_CHECK_LIVE | RQ_CHECK_CANCEL))
    return -ERR_STATE;
  positional = !!(slot->args[3] & FS_IO_POSITIONAL);
  writing = call->words[0] == FS_WRITE;
  offset = positional ? slot->args[0] : *position;
  reply.words[1] = slot->seq;
  if (work->written)
    bytes = work->written;
  else if (state & RQ_CHECK_CANCEL)
    reply.words[0] = (u64) -EINTR;
  else if (work->failed)
    reply.words[0] = (u64) -EIO;
  else if (sysfileargs(call->words[0], slot) || offset > (u64) INT64_MAX ||
           (!positional && slot->args[0]))
    reply.words[0] = (u64) -EINVAL;
  else if (writing && (!loads || (slot->args[3] & FS_IO_APPEND)))
    reply.words[0] = (u64) -ENOSYS;
  else if (slot->args[1] && (writing || offset < map->file.length)) {
    if (writing) {
      Writeplan plan;
      error = loadmapwrite(map, index, first, (u32) cache->blocks, offset, slot->args[1], &plan);
      if (error == -ERR_OVERFLOW || error == -ERR_BUSY ||
          (!error && (!plan.block || plan.length != map->file.length))) {
        reply.words[0] = (u64) (error == -ERR_OVERFLOW ? -EFBIG : -ENOSYS);
        goto respond;
      }
      if (!error) {
        span = (Loadspan){plan.block, 0, plan.offset};
        bytes = plan.bytes;
      }
    } else {
      bytes = MIN(slot->args[1], map->file.length - offset);
      error = loadmapspan(&map->file, map, index, first, (u32) cache->blocks, offset, bytes, &span);
    }
    if (!error && !work->span.cache)
      error = cachespanbegin(cache, span.first, span.next, span.offset, (u32) bytes, &work->span);
    if (error == -ERR_WOULD_BLOCK || (!writing && error == -ERR_CAPACITY))
      return -ERR_WOULD_BLOCK;
    if (!error) {
      work->request = call->request;
      blocks[0] = span.first;
      blocks[1] = span.next;
      for (i = 0; i < ARRAY_SIZE(blocks); i++) {
        u8 *destination;
        if (!blocks[i])
          continue;
        error =
            cachefillstart(&work->fill, cache, blocks[i], CACHE_DATA, &fetch->ticket, &destination);
        if (error == 1) {
          fetch->block = blocks[i];
          return 1;
        }
        if (error == -ERR_WOULD_BLOCK)
          return error;
        if (error)
          break;
      }
      if (!error && writing) {
        u64 guard;
        error = loadwrite(loads, map->file.inode, &guard);
        if (!error) {
          u8 *input = scratch;
          for (i = 0; i < bytes; i++)
            input[i] = page[slot->args[2] + i];
          error = cacheedit(cache, &work->span.keys[0], span.offset, input, (u32) bytes);
          if (!error) {
            work->written = (u32) bytes;
            if (!positional)
              *position = offset + bytes;
          }
          if (loadunwrite(loads, guard))
            return -ERR_STATE;
        }
      } else if (!error)
        error = cachespancopy(&work->span, scratch);
    }
    if (error) {
      bytes = 0;
      reply.words[0] = (u64) (writing && error == -ERR_BUSY       ? -ETXTBSY
                              : writing && error == -ERR_CAPACITY ? -EAGAIN
                                                                  : -EIO);
    }
  }
respond:
  if (!writing) {
    for (i = 0; i < bytes; i++)
      page[slot->args[2] + i] = data[i];
    reply.words[3] = bytes;
  }
  reply.words[2] = bytes;
  error = ipc_reply(call->request, &reply);
  if (error)
    return error;
  if (!writing && !positional && bytes)
    *position = offset + bytes;
  if (work->span.cache)
    error = cachespanend(&work->span);
finished:
  if (!error)
    error = sysfilefinish(active, call->request);
  if (!error)
    work->request = work->failed = work->written = 0;
  return error;
}
