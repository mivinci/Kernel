/* TI PC16550D SNLS378C 8.3; Arm PL011 DDI0183G chapters 2/3. */
#include "uart.h"
#include "private/io.h"

static u32
readreg(Uart *uart, u32 offset)
{
  u64 address = uart->base + ((u64) offset << uart->shift);
  return uart->width == 4 ? ioread32(address) : ioread8(address);
}
static void
writereg(Uart *uart, u32 offset, u32 value)
{
  u64 address = uart->base + ((u64) offset << uart->shift);
  if (uart->width == 4)
    iowrite32(address, value);
  else
    iowrite8(address, value);
}
static isize
ready(Uart *uart)
{
  if (!uart)
    return -ERR_INVALID;
  return uart->initialized ? 0 : -ERR_STATE;
}
static void
increment(u64 *counter)
{
  if (*counter != UINT64_MAX)
    (*counter)++;
}
static void
errors(Uart *uart, u32 bits)
{
  if (bits & 1)
    increment(&uart->overruns);
  if (bits & 2)
    increment(&uart->parity);
  if (bits & 4)
    increment(&uart->framing);
  if (bits & 8)
    increment(&uart->breaks);
}
static u32
status16550(Uart *uart)
{
  u32 status = readreg(uart, 5);
  /* TX/drain polling must not swallow read-to-clear receiver error bits. */
  uart->saved |= status & 0x1e;
  return status;
}
isize
uartinit(Uart *uart, u64 base, u64 bytes, u32 kind, u32 shift, u32 width)
{
  volatile u8 *memory;
  u64          last;
  u32          i, control;
  if (!uart || !base || shift > 4 || (width != 1 && width != 4) ||
      (kind != BOOT_UART_NS16550 && kind != BOOT_UART_PL011) ||
      (kind == BOOT_UART_PL011 && (shift || width != 4)) ||
      (kind == BOOT_UART_NS16550 && ((UINT64_C(1) << shift) & (width - 1))) ||
      !IS_ALIGNED(base, width) || bytes > UINT64_MAX - base)
    return -ERR_INVALID;
  last = (u64) (kind == BOOT_UART_PL011 ? 0x48 : 5) << shift;
  if (bytes < last + width)
    return -ERR_INVALID;
  memory = (volatile u8 *) uart;
  for (i = 0; i < sizeof(*uart); i++)
    memory[i] = 0;
  uart->base = base;
  uart->bytes = bytes;
  uart->kind = kind;
  uart->shift = shift;
  uart->width = width;
  if (kind == BOOT_UART_NS16550) {
    control = readreg(uart, 3);
    writereg(uart, 3, control & ~0x80U);
    writereg(uart, 1, 0);
    writereg(uart, 4, (readreg(uart, 4) & ~0x10U) | 0x0b);
    writereg(uart, 2, 7);
    uart->fifo = (readreg(uart, 2) & 0xc0) == 0xc0 ? 16 : 1;
  } else {
    writereg(uart, 0x38, 0);
    control = readreg(uart, 0x30);
    writereg(uart, 0x30, control & ~1U);
    if (readreg(uart, 0x18) & 8)
      return -ERR_BUSY;
    writereg(uart, 0x48, 0);
    writereg(uart, 0x44, 0x7ff);
    writereg(uart, 4, 0);
    writereg(uart, 0x34, 0);
    writereg(uart, 0x30, (control & ~0xc086U) | 0x301);
    uart->fifo = 1;
  }
  uart->initialized = 1;
  return 0;
}
isize
uartmask(Uart *uart, u32 receive, u32 transmit)
{
  isize error = ready(uart);
  u32   mask;
  if (error)
    return error;
  if (receive > 1 || transmit > 1)
    return -ERR_INVALID;
  mask = uart->kind == BOOT_UART_NS16550 ? (receive ? 5 : 0) | (transmit ? 2 : 0)
                                         : (receive ? 0x7d0 : 0) | (transmit ? 0x20 : 0);
  writereg(uart, uart->kind == BOOT_UART_NS16550 ? 1 : 0x38, mask);
  uart->mask = mask;
  return 0;
}
isize
uartrx(Uart *uart, void *data, u32 bytes, u32 *more)
{
  u8   *out = data;
  u32   attempts, copied = 0, status, byte, bits;
  isize error = ready(uart);
  if (error)
    return error;
  if (!out || !more || !bytes || bytes > SERIAL_BATCH)
    return -ERR_INVALID;
  *more = 0;
  for (attempts = 0; attempts < SERIAL_BATCH && copied < bytes; attempts++) {
    if (uart->kind == BOOT_UART_NS16550) {
      status = status16550(uart);
      bits = uart->saved >> 1;
      uart->saved = 0;
      errors(uart, bits);
      if (!(status & 1))
        break;
      byte = readreg(uart, 0);
    } else {
      if (readreg(uart, 0x18) & 0x10)
        break;
      byte = readreg(uart, 0);
      status = (byte >> 8) & 15;
      bits = ((status & 8) >> 3) | (status & 2) | ((status & 1) << 2) | ((status & 4) << 1);
      errors(uart, bits);
    }
    increment(&uart->received);
    if (bits & 14)
      increment(&uart->dropped);
    else
      out[copied++] = (u8) byte;
  }
  *more = attempts == SERIAL_BATCH || copied == bytes;
  if (uart->kind == BOOT_UART_PL011 && !*more) {
    writereg(uart, 4, 0);
    writereg(uart, 0x44, 0x7d0);
  }
  return copied;
}
isize
uarttx(Uart *uart, const void *data, u32 bytes)
{
  const u8 *in = data;
  u32       count, limit;
  isize     error = ready(uart);
  if (error)
    return error;
  if (!in || !bytes || bytes > SERIAL_BATCH)
    return -ERR_INVALID;
  limit = bytes;
  if (uart->kind == BOOT_UART_NS16550) {
    if (!(status16550(uart) & 0x20))
      return 0;
    limit = MIN(limit, uart->fifo);
  }
  for (count = 0; count < limit; count++) {
    if (uart->kind == BOOT_UART_PL011 && (readreg(uart, 0x18) & 0x20))
      break;
    writereg(uart, 0, in[count]);
    increment(&uart->transmitted);
  }
  return count;
}
isize
uartdrained(Uart *uart)
{
  isize error = ready(uart);
  u32   status;
  if (error)
    return error;
  status = uart->kind == BOOT_UART_NS16550 ? status16550(uart) : readreg(uart, 0x18);
  return uart->kind == BOOT_UART_NS16550 ? (status & 0x60) == 0x60 : (status & 0x88) == 0x80;
}
