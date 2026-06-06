#include <kernel.h>
#include <libc.h>
#include <uart.h>

void uart_init(void) {
  /* Disable interrupts during init */
  WRITE(IER, 0);

  /* Set baud rate: 115200 = 115200 Hz, divisor = 38.4k / 115200 ≈ 0.333
   * For QEMU the divisor doesn't matter, but we set it anyway. */
  WRITE(LCR, LCR_DLAB);
  WRITE(DLL, 0x01); /* 115200 baud with 1.8432 MHz clock: divisor = 1 */
  WRITE(DLM, 0x00);
  WRITE(LCR, LCR_8BIT); /* 8 data bits, no parity, 1 stop bit */

  /* Enable FIFO */
  WRITE(FCR, FCR_ENABLE);

  /* Enable interrupt output (OUT2) */
  WRITE(MCR, MCR_OUT2);

  /* Enable receive interrupt */
  WRITE(IER, IER_RDA);
}

/* 同步发送一个字符 */
void putc(char c) {
  while ((READ(LSR) & LSR_TX_READY) == 0);
  WRITE(THR, c);
}

/* 同步发送一个字符串 */
void puts(char *p) {
  char c;
  while ((c = *p++) != '\0') {
    putc(c);
    if (c == '\n')
      putc('\r');
  }
}

/*
 * Polling-based character receive. Returns -1 if no data available.
 */
int getc(void) {
  if (READ(LSR) & LSR_RX_READY)
    return (unsigned char)READ(RBR);
  return -1;
}

/*
 * Handle a UART interrupt (called from PLIC handler).
 * Reads all available received characters and echoes them.
 */
void uart_handle(void) {
  while (READ(LSR) & LSR_RX_READY) {
    char c = READ(RBR);
    putc(c); /* echo */
  }
}

int printk(const char *fmt, ...) {
  va_list args;
  char buf[1024];

  va_start(args, fmt);
  vsprintf(buf, fmt, args);
  va_end(args);

  puts(buf);
  return 0;
}
