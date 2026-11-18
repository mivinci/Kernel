#include "../../sbin/serial/uart.h"
#include "private/io.h"
u32
uartusercheck(u64 base, u32 kind)
{
  Uart  uart;
  u32   word, receive, transmit, mask, more;
  u8    data[SERIAL_BATCH];
  isize result;
  /* Exercise all ISA access widths, including the C ABI for a u32 with bit31. */
  iowrite32((u64) &word, 0xfedcba98U);
  if (ioread32((u64) &word) != 0xfedcba98U)
    return 1;
  iowrite8((u64) &word, 0x35);
  if (ioread8((u64) &word) != 0x35 || ioread32((u64) &word) != 0xfedcba35U)
    return 2;
  if (uartinit(&uart, base, PAGE_SIZE, kind, 0, kind == BOOT_UART_PL011 ? 4 : 1))
    return 3;
  for (receive = 0; receive < 2; receive++) {
    for (transmit = 0; transmit < 2; transmit++) {
      if (uartmask(&uart, receive, transmit))
        return 4;
      mask = kind == BOOT_UART_NS16550 ? ioread8(base + 1) : ioread32(base + 0x38);
      if (mask != uart.mask)
        return 5;
    }
  }
  if (uartmask(&uart, 0, 0) || uartrx(&uart, data, SERIAL_BATCH, &more) || more)
    return 6;
  data[0] = '\n';
  if (uarttx(&uart, data, 1) != 1 || uart.transmitted != 1 || uart.received)
    return 7;
  result = uartdrained(&uart);
  return result == 0 || result == 1 ? 42 : 8;
}
