#include "serial.h"
#include "private/serial.h"

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
