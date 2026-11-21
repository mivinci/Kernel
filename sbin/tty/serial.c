#include "serial.h"
#include "private/serial.h"
#include <errno.h>

isize
ttyserialinit(Ttyserial *io, Serialring *rx, Serialring *tx, u64 epoch)
{
  isize error;
  if (!io || !rx || !tx || rx == tx)
    return -ERR_INVALID;
  error = serialread(rx, epoch, NULL, 0);
  if (!error)
    error = serialwrite(tx, epoch, NULL, 0);
  if (error)
    return error;
  io->rx = rx;
  io->tx = tx;
  io->epoch = epoch;
  io->queued = 0;
  return 0;
}

isize
ttyserialoutput(Ttyserial *io, u64 oflag, const void *data, u32 size)
{
  u32   accepted;
  isize encoded;
  if (!io || !io->tx || size > TTY_BATCH || (size && !data))
    return -ERR_INVALID;
  if (io->queued)
    return 0;
  encoded = ttyencode(oflag, data, size, io->output, 2 * TTY_BATCH, &accepted);
  if (encoded < 0)
    return encoded;
  io->queued = (u32) encoded;
  return accepted;
}

isize
ttyserialwrite(Ttyserial *io, u64 oflag, const Fileslot *slot, const volatile u8 *page)
{
  u8    snapshot[TTY_BATCH];
  u32   count, i;
  isize status = sysfileargs(FS_WRITE, slot);
  if (status)
    return status;
  if (!io || !io->tx || (slot->args[1] && !page))
    return -ERR_INVALID;
  if (slot->args[3] & FS_IO_POSITIONAL)
    return -ERR_NOTSUP;
  count = (u32) MIN(slot->args[1], TTY_BATCH);
  if (!count)
    return 0;
  if (io->queued)
    return -ERR_WOULD_BLOCK;
  for (i = 0; i < count; i++)
    snapshot[i] = page[slot->args[2] + i];
  return ttyserialoutput(io, oflag, snapshot, count);
}

isize
ttyserialread(Ttyinput *input, const Fileslot *slot, volatile u8 *page)
{
  u32   ended;
  isize status = sysfileargs(FS_READ, slot);
  if (status)
    return status;
  if (slot->args[3] & FS_IO_POSITIONAL)
    return -ERR_NOTSUP;
  if (!slot->args[1])
    return ttyinputcopy(input, NULL, 0, &ended);
  if (!page)
    return -ERR_INVALID;
  return ttyinputcopy(input, page + slot->args[2], (u32) slot->args[1], &ended);
}

isize
ttyfileio(Ttyserial *io, Ttyinput *input, u64 oflag, Fileinflight *active, const Ipcmsg *call,
          const Fileslot *slot, volatile u8 *page, u32 nonblock)
{
  Msg   reply = {{0, 0, 0, 0}};
  u64   state, opcode;
  isize result, error;
  if (!active || !call || call->kind != IPC_CALL || !slot || nonblock > 1 ||
      call->words[3] >= FS_CONN_SLOTS || active->requests[call->words[3]] != call->request ||
      !call->request || !slot->seq || slot->seq != call->words[2])
    return -ERR_INVALID;
  opcode = call->words[0];
  if (opcode != FS_READ && opcode != FS_WRITE)
    return -ERR_INVALID;
  error = ipc_rqcheck(call->request, &state);
  if (error)
    return error;
  if (state == RQ_CHECK_DRAINING) {
    error = ipc_rqfinish(call->request);
    return error ? error : sysfilefinish(active, call->request);
  }
  reply.words[1] = slot->seq;
  if (state & RQ_CHECK_CANCEL)
    reply.words[0] = (u64) -EINTR;
  else {
    result = opcode == FS_READ ? ttyserialread(input, slot, page)
                               : ttyserialwrite(io, oflag, slot, page);
    if (result == -ERR_WOULD_BLOCK && !nonblock)
      return result;
    if (result < 0)
      reply.words[0] = (u64) (result == -ERR_WOULD_BLOCK ? -EAGAIN
                              : result == -ERR_INVALID   ? -EINVAL
                              : result == -ERR_NOTSUP    ? -ESPIPE
                                                         : -EIO);
    else {
      reply.words[2] = (u64) result;
      if (opcode == FS_READ)
        reply.words[3] = (u64) result;
    }
  }
  error = ipc_reply(call->request, &reply);
  return error ? error : sysfilefinish(active, call->request);
}

isize
ttyserialstep(Ttyserial *io, Ttyinput *input, const Termarg *mode, u32 rxlimit,
              Ttyserialresult *out)
{
  Ttyeffect effect;
  u32       i, received, accepted, full = 0;
  u8        byte;
  isize     count;
  if (!io || !io->rx || !io->tx || !input || !mode || !out || rxlimit > SERIAL_BATCH)
    return -ERR_INVALID;
  *out = (Ttyserialresult){0};
  if (io->queued > sizeof(io->output))
    return -ERR_STATE;
  count = ttyattrcheck(mode, mode->ispeed);
  if (count)
    return count;
  if (input->canonical != !!(mode->lflag & TTY_ICANON))
    return -ERR_STATE;
  /* Reserve space for the worst case echo before advancing the RX consumer. */
  for (received = 0; received < rxlimit; received++) {
    u64 end;
    if ((mode->lflag & TTY_ECHO) && sizeof(io->output) - io->queued < 2 * sizeof(effect.echo)) {
      full = 1;
      break;
    }
    count = serialread(io->rx, io->epoch, &byte, 1);
    if (!count) {
      end = __atomic_load_n(&io->rx->flags, __ATOMIC_ACQUIRE);
      if (!end)
        break;
      /* A final producer update may have landed between the empty read
         and the flag load. Observe the position again after the flag. */
      count = serialread(io->rx, io->epoch, &byte, 1);
      if (!count)
        out->end = end;
    }
    if (count < 0)
      return count;
    if (!count)
      break;
    out->hints |= SERIAL_RX_ROOM;
    count = ttyinputbyte(input, mode, byte, &effect);
    if (count)
      return count;
    if (effect.flags & TTY_FLUSH_OUTPUT)
      io->queued = 0;
    count = ttyencode(mode->oflag, effect.echo, effect.bytes, io->output + io->queued,
                      MIN(2 * TTY_BATCH, sizeof(io->output) - io->queued), &accepted);
    if (count < 0)
      return count;
    if (accepted != effect.bytes)
      return -ERR_STATE;
    io->queued += (u32) count;
    if (effect.signal) {
      out->signal = effect.signal;
      out->again = 1;
      return 0;
    }
  }
  if (!io->queued) {
    out->again = rxlimit && received == rxlimit;
    return 0;
  }
  count = serialwrite(io->tx, io->epoch, io->output, MIN(io->queued, SERIAL_BATCH));
  if (count < 0)
    return count;
  if (count) {
    io->queued -= (u32) count;
    for (i = 0; i < io->queued; i++)
      io->output[i] = io->output[i + (u32) count];
    out->hints |= SERIAL_TX_READY;
  }
  out->again = (rxlimit && received == rxlimit) || (count && (full || io->queued));
  return 0;
}

isize
ttyserialpump(Ttyserial *io, Ttyinput *input, const Termarg *mode, u32 rxlimit, Handle notify,
              Ttyserialresult *out)
{
  isize error, notified = 0;
  if (!out || notify <= 0)
    return -ERR_INVALID;
  *out = (Ttyserialresult){0};
  error = ttyserialstep(io, input, mode, rxlimit, out);
  if (out->hints)
    notified = ipc_notify(notify, out->hints);
  return error ? error : notified;
}
