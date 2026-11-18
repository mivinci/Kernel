#include "io.h"
#include "private/serial.h"

static isize
queued(Serialring *ring, u32 *bytes)
{
  u64 producer = __atomic_load_n(&ring->producer, __ATOMIC_ACQUIRE);
  u64 consumer = __atomic_load_n(&ring->consumer, __ATOMIC_ACQUIRE);
  if (consumer > producer || producer - consumer > SERIAL_CAPACITY)
    return -ERR_PEER;
  *bytes = (u32) (producer - consumer);
  return 0;
}

isize
serialioinit(Serialio *io, Uart *uart, Serialring *rx, Serialring *tx, u64 epoch)
{
  u32   i;
  isize error;
  if (!io || !uart || !uart->initialized || !rx || !tx || rx == tx)
    return -ERR_INVALID;
  error = serialwrite(rx, epoch, NULL, 0);
  if (!error)
    error = serialread(tx, epoch, NULL, 0);
  if (error)
    return error;
  io->uart = uart;
  io->rx = rx;
  io->tx = tx;
  io->epoch = epoch;
  io->count = io->offset = 0;
  for (i = 0; i < SERIAL_BATCH; i++)
    io->pending[i] = 0;
  return 0;
}

isize
serialiostep(Serialio *io, u64 *hints, u32 *again)
{
  u8    data[SERIAL_BATCH];
  u32   used, room, more = 0, sent = 0;
  isize error, count;
  if (!io || !io->uart || !io->rx || !io->tx || !hints || !again)
    return -ERR_INVALID;
  *hints = 0;
  *again = 0;
  if (io->offset > io->count || io->count > SERIAL_BATCH) {
    error = -ERR_STATE;
    goto failed;
  }
  /* Validate both shared endpoints before touching device state. */
  error = serialwrite(io->rx, io->epoch, NULL, 0);
  if (!error)
    error = serialread(io->tx, io->epoch, NULL, 0);
  if (!error)
    error = queued(io->rx, &used);
  if (error)
    goto failed;
  room = SERIAL_CAPACITY - used;
  if (room) {
    count = uartrx(io->uart, data, MIN(room, SERIAL_BATCH), &more);
    if (count < 0) {
      error = count;
      goto failed;
    }
    if (count) {
      error = serialwrite(io->rx, io->epoch, data, (u32) count);
      if (error != count) {
        if (error >= 0)
          error = -ERR_PEER;
        goto failed;
      }
      *hints |= SERIAL_RX_READY;
    }
  }
  if (io->count == io->offset) {
    count = serialread(io->tx, io->epoch, io->pending, SERIAL_BATCH);
    if (count < 0) {
      error = count;
      goto failed;
    }
    io->count = (u32) count;
    io->offset = 0;
    if (count)
      *hints |= SERIAL_TX_ROOM;
  }
  if (io->count) {
    count = uarttx(io->uart, io->pending + io->offset, io->count - io->offset);
    if (count < 0) {
      error = count;
      goto failed;
    }
    sent = (u32) count;
    io->offset += sent;
    if (io->count == io->offset)
      io->count = io->offset = 0;
  }
  error = queued(io->rx, &used);
  if (error)
    goto failed;
  room = SERIAL_CAPACITY - used;
  error = queued(io->tx, &used);
  if (error)
    goto failed;
  error = uartmask(io->uart, room != 0, io->count != 0 || used != 0);
  if (error)
    goto failed;
  *again = (more && room) || (io->count ? sent != 0 : used != 0);
  return 0;
failed:
  (void) uartmask(io->uart, 0, 0);
  *again = 0;
  return error;
}
