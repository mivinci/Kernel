#include <kernel.h>
#include <types.h>
#include <tty.h>
#include <uart.h>

unsigned long uart_mmio_base = 0x10000000UL;

void uart_init(void) {
  WRITE(IER, 0);
  WRITE(LCR, LCR_DLAB);
  WRITE(DLL, 0x01);
  WRITE(DLM, 0x00);
  WRITE(LCR, LCR_8BIT);
  WRITE(FCR, FCR_ENABLE);
  WRITE(MCR, MCR_OUT2);
  WRITE(IER, IER_RDA);
}

void putc(char c) {
  while ((READ(LSR) & LSR_TX_READY) == 0)
    ;
  WRITE(THR, c);
}

void puts(char *p) {
  char c;
  while ((c = *p++) != '\0') {
    putc(c);
    if (c == '\n') putc('\r');
  }
}

void uart_handle(void) {
  while (READ(LSR) & LSR_RX_READY)
    tty_input(&console_tty, READ(RBR));
}

int printk(const char *fmt, ...) {
  va_list args;
  char    buf[1024];
  va_start(args, fmt);
  vsprintf(buf, fmt, args);
  va_end(args);
  puts(buf);
  return 0;
}
