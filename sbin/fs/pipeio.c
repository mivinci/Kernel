#include "pipeio.h"
#include <errno.h>

isize
pipeiostart(Pipeio *io, const Fileinflight *active, const Ipcmsg *call, const Fileslot *slot,
            volatile u8 *page, u32 end, u32 nonblock)
{
  u64 opcode, index;
  u32 i;
  if (!io || !active || !call || !slot || end > PIPE_WRITE || nonblock > 1)
    return -ERR_INVALID;
  if (io->request)
    return -ERR_BUSY;
  index = call->words[3];
  opcode = call->words[0];
  if (call->kind != IPC_CALL || handlekind(call->request) != HANDLE_REQUEST ||
      index >= FS_CONN_SLOTS || active->requests[index] != call->request || !slot->seq ||
      slot->seq != call->words[2])
    return -ERR_STALE;
  if (opcode != (end == PIPE_READ ? FS_READ : FS_WRITE))
    return -ERR_DENIED;
  if (sysfileargs(opcode, slot) || (slot->args[1] && !page))
    return -ERR_INVALID;
  io->request = call->request;
  io->slot = (u32) index;
  io->end = end;
  io->nonblock = nonblock;
  io->bytes = (u32) slot->args[1];
  io->page = page ? page + slot->args[2] : NULL;
  io->reply.words[0] = io->reply.words[2] = io->reply.words[3] = 0;
  io->reply.words[1] = slot->seq;
  io->fixed = 0;
  io->positional = !!(slot->args[3] & FS_IO_POSITIONAL);
  if (!io->positional && end == PIPE_WRITE)
    for (i = 0; i < io->bytes; i++)
      io->data[i] = io->page[i];
  return 0;
}

isize
pipeiostep(Pipeio *io, Pipe *pipe, Fileinflight *active)
{
  u64   state;
  u32   i;
  isize result, error;
  if (!io || !pipe || !active || !io->request || io->slot >= FS_CONN_SLOTS ||
      active->requests[io->slot] != io->request)
    return -ERR_STALE;
  error = ipc_rqcheck(io->request, &state);
  if (error)
    return error;
  if (state != RQ_CHECK_LIVE && state != (RQ_CHECK_LIVE | RQ_CHECK_CANCEL) &&
      state != RQ_CHECK_DRAINING)
    return -ERR_STATE;
  if (state == RQ_CHECK_DRAINING)
    error = ipc_rqfinish(io->request);
  else {
    if (!io->fixed) {
      if (state & RQ_CHECK_CANCEL)
        io->reply.words[0] = (u64) -EINTR;
      else if (io->positional)
        io->reply.words[0] = (u64) -ESPIPE;
      else {
        result = io->end == PIPE_READ ? piperead(pipe, io->data, io->bytes)
                                      : pipewrite(pipe, io->data, io->bytes);
        if (result == -ERR_WOULD_BLOCK && !io->nonblock)
          return result;
        if (result < 0)
          io->reply.words[0] = (u64) (result == -ERR_WOULD_BLOCK ? -EAGAIN
                                      : result == -ERR_PEER      ? -EPIPE
                                                                 : -EIO);
        else {
          io->reply.words[2] = (u64) result;
          if (io->end == PIPE_READ) {
            for (i = 0; i < (u32) result; i++)
              io->page[i] = io->data[i];
            io->reply.words[3] = (u64) result;
          }
        }
      }
      io->fixed = 1;
    }
    error = ipc_reply(io->request, &io->reply);
  }
  if (!error) {
    error = sysfilefinish(active, io->request);
    if (!error)
      io->request = 0;
  }
  return error;
}
