#ifndef _SERIAL_UART_H
#define _SERIAL_UART_H
#include <types.h>
#include <abi/base.h>
#include <abi/system.h>
#include <proto/serial.h>

typedef struct Uart Uart;
struct Uart
{
  u64 base, bytes;
  u32 kind, shift, width, fifo, initialized, mask, saved;
  u64 received, transmitted, overruns, parity, framing, breaks, dropped;
};
/* Exclusive valid Device mapping, firmware clock/pinmux/baud already prepared.
   Call before online: disables interrupts and discards pre-READY NS16550 FIFO
   contents. Preserves baud/line format; does not program clocks or enable DMA.
   PL011 BUSY returns ERR_BUSY with UART disabled/IRQ masked and initialized=0;
   caller may retry later after the current character completes. Never spin.
   No concurrent polling console or second device owner is permitted. */
isize uartinit(Uart *uart, u64 base, u64 bytes, u32 kind, u32 shift, u32 width);
isize uartmask(Uart *uart, u32 receive, u32 transmit);
/* Valid private buffers, disjoint from uart and its MMIO. At most 32 bytes or
   receive attempts. more is conservative: service must continue/yield rather
   than sleep awaiting another hint. Bad characters are counted and dropped;
   overrun alone does not invalidate the queued character. */
isize uartrx(Uart *uart, void *data, u32 bytes, u32 *more);
isize uarttx(Uart *uart, const void *data, u32 bytes);
/* Physical FIFO AND shift-register drain only, not the software TX queue.
   Reading NS16550 status preserves its read-to-clear RX errors for uartrx. */
isize uartdrained(Uart *uart);
#endif
