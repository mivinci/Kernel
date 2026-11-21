#include "read.h"
#include <errno.h>

isize
fileread(Fileinflight *active, const Ipcmsg *call, const Fileslot *slot, const Loadmap *map,
         const Loadindex *index, Cache *cache, u32 first, u64 *position, void *scratch,
         volatile u8 *page)
{
  Msg       reply = {{0, 0, 0, 0}};
  Loadspan  span;
  u64       state, at, offset, bytes = 0, i;
  const u8 *data = scratch;
  u32       positional;
  isize     error;
  if (!active || !call || !slot || !map || !cache || cache->blocks > UINT32_MAX || !position ||
      !scratch || !page || call->kind != IPC_CALL || call->words[0] != FS_READ ||
      handlekind(call->request) != HANDLE_REQUEST)
    return -ERR_INVALID;
  at = call->words[3];
  if (at >= FS_CONN_SLOTS || active->requests[at] != call->request || !slot->seq ||
      slot->seq != call->words[2])
    return -ERR_STALE;
  error = ipc_rqcheck(call->request, &state);
  if (error)
    return error;
  if (state == RQ_CHECK_DRAINING) {
    error = ipc_rqfinish(call->request);
    return error ? error : sysfilefinish(active, call->request);
  }
  if (state != RQ_CHECK_LIVE && state != (RQ_CHECK_LIVE | RQ_CHECK_CANCEL))
    return -ERR_STATE;
  positional = !!(slot->args[3] & FS_IO_POSITIONAL);
  offset = positional ? slot->args[0] : *position;
  reply.words[1] = slot->seq;
  if (state & RQ_CHECK_CANCEL)
    reply.words[0] = (u64) -EINTR;
  else if (sysfileargs(FS_READ, slot) || offset > (u64) INT64_MAX || (!positional && slot->args[0]))
    reply.words[0] = (u64) -EINVAL;
  else if (offset < map->file.length && slot->args[1]) {
    bytes = MIN(slot->args[1], map->file.length - offset);
    error = loadmapspan(&map->file, map, index, first, (u32) cache->blocks, offset, bytes, &span);
    if (!error)
      error = cachecopydata(cache, span.first, span.next, span.offset, scratch, (u32) bytes);
    if (error == -ERR_WOULD_BLOCK || error == -ERR_CAPACITY)
      return -ERR_WOULD_BLOCK;
    if (error) {
      bytes = 0;
      reply.words[0] = (u64) -EIO;
    }
  }
  for (i = 0; i < bytes; i++)
    page[slot->args[2] + i] = data[i];
  reply.words[2] = reply.words[3] = bytes;
  error = ipc_reply(call->request, &reply);
  if (error)
    return error;
  if (!positional && bytes)
    *position = offset + bytes;
  return sysfilefinish(active, call->request);
}
