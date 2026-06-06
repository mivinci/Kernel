#include <kernel.h>
#include <libc.h>
#include <uart.h>

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


int printk(const char *fmt, ...) {
  va_list args;
  char buf[1024];

  va_start(args, fmt);
  vsprintf(buf, fmt, args);
  va_end(args);

  puts(buf);
  return 0;
}
