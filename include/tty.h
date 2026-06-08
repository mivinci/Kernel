#ifndef _TTY_H
#define _TTY_H

#include <spinlock.h>
#include <types.h>

/* Forward: Proc is defined in proc.h */
struct Proc_s;

/* TTY control characters */
#define TTY_CTRL_C  0x03   /* ^C → SIGINT */
#define TTY_CTRL_D  0x04   /* ^D → EOF (on empty line) */
#define TTY_BS      0x7f   /* backspace */
#define TTY_CTRL_U  0x15   /* ^U → erase line */

/* Signals (minimal subset) */
#define SIGINT      2

/* Cooked line buffer size */
#define TTY_LINE_MAX  256

XDEF_STRUCT(Tty) {
  SpinLock lock;
  char     line_buf[TTY_LINE_MAX];
  int      line_len;
  int      eof;
  struct Proc_s *reader;
  int      echo;
  int      fg_pgid;
};

extern Tty console_tty;

void tty_init(Tty *tty);
void tty_input(Tty *tty, char c);
int  tty_read(Tty *tty, char *ubuf, int n);

#endif /* _TTY_H */
