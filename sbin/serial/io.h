#ifndef _SERIAL_IO_H
#define _SERIAL_IO_H
#include "uart.h"

typedef struct Serialio Serialio;
struct Serialio
{
  Uart       *uart;
  Serialring *rx, *tx;
  u64         epoch;
  u32         count, offset;
  u8          pending[SERIAL_BATCH];
};
/* Exclusive initialized UART and two distinct ordinary coherent rings, valid
   for the lifetime of io. io/uart/MMIO/rings are disjoint. The service alone
   produces RX and consumes TX; the peer owns the opposite positions. */
isize serialioinit(Serialio *io, Uart *uart, Serialring *rx, Serialring *tx, u64 epoch);
/* At most 32 RX attempts and 32 TX bytes per step. Partial hardware TX stays
   in the private bounded pending buffer after advancing the shared consumer.
   Output hints are RX_READY/TX_ROOM only; the caller sends/coalesces them.
   again requires a yield and another step, never waiting for another hint.
   Otherwise sleep is permitted only through the service's persistent-event
   protocol. Device IRQ masks follow RX room and outstanding TX; kernel IRQ
   ACK belongs to the caller after consuming and servicing an IRQ event.
   Failure masks device IRQs; it does not unmap/reset/free either live ring. */
isize serialiostep(Serialio *io, u64 *hints, u32 *again);
#endif
