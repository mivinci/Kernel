#include <chr.h>
#include <kernel.h>
#include <types.h>
#include <proc.h>
#include <spinlock.h>
#include <tty.h>

Tty console_tty;

static void tty_wake_reader(Tty *tty) {
  if (!tty->reader) return;
  spin_lock(&ptable_lock);
  if (tty->reader->state == IOWAIT)
    tty->reader->state = RUNNABLE;
  spin_unlock(&ptable_lock);
}

static void tty_send_sigint(Tty *tty) {
  spin_lock(&ptable_lock);
  for (int i = 0; i < NPROC; i++) {
    if (ptable[i].state != UNUSED && ptable[i].pgid == tty->fg_pgid)
      ptable[i].sig_pending |= SIGINT;
  }
  spin_unlock(&ptable_lock);
}

void tty_init(Tty *tty) {
  spin_init(&tty->lock);
  tty->line_len = 0;
  tty->eof      = 0;
  tty->reader   = NULL;
  tty->echo     = 1;
  tty->fg_pgid  = 1;
}

void tty_input(Tty *tty, char c) {
  /*
   * Collect echo characters to write outside the lock.
   * chr_write may busy-wait on TX_READY; holding tty->lock
   * across that wait would block any tty_read caller and
   * freeze the terminal under multi-hart TX contention.
   */
  char echo_buf[4];
  int  echo_n = 0;
  int  wake   = 0;

  spin_lock(&tty->lock);

  switch ((unsigned char)c) {
  case TTY_CTRL_C:
    spin_unlock(&tty->lock);
    tty_send_sigint(tty);
    spin_lock(&tty->lock);
    tty->eof = 1;
    wake = 1;
    break;
  case TTY_CTRL_D:
    if (tty->line_len == 0) {
      tty->eof = 1;
      wake = 1;
    }
    break;
  case TTY_BS:
    if (tty->line_len > 0) {
      tty->line_len--;
      if (tty->echo) {
        echo_buf[echo_n++] = '\b';
        echo_buf[echo_n++] = ' ';
        echo_buf[echo_n++] = '\b';
      }
    }
    break;
  case TTY_CTRL_U:
    if (tty->echo) {
      for (int i = 0; i < tty->line_len; i++) {
        if (echo_n < 3) { echo_buf[echo_n++] = '\b'; echo_buf[echo_n++] = ' '; echo_buf[echo_n++] = '\b'; }
      }
    }
    tty->line_len = 0;
    break;
  case '\r':
    c = '\n';
    /* fall through */
  case '\n':
    if (tty->line_len < TTY_LINE_MAX - 1)
      tty->line_buf[tty->line_len++] = '\n';
    if (tty->echo) { echo_buf[echo_n++] = '\r'; echo_buf[echo_n++] = '\n'; }
    wake = 1;
    break;
  default:
    if (c >= ' ' && tty->line_len < TTY_LINE_MAX - 1) {
      tty->line_buf[tty->line_len++] = c;
      if (tty->echo) echo_buf[echo_n++] = c;
    }
    break;
  }

  if (wake) tty_wake_reader(tty);
  spin_unlock(&tty->lock);

  /* Echo after unlocking so TX-busy doesn't block tty_read */
  for (int i = 0; i < echo_n; i++)
    chr_write(echo_buf[i]);
}

int tty_read(Tty *tty, char *ubuf, int n) {
  Proc *cur = cpu_proc();
  if (!cur) return -1;

  spin_lock(&tty->lock);

  while (tty->line_len == 0 && !tty->eof) {
    if (tty->reader && tty->reader != cur) {
      spin_unlock(&tty->lock);
      return -1;
    }
    tty->reader = cur;
    cur->state  = IOWAIT;
    spin_unlock(&tty->lock);
    yield();
    /* Poll for chars that missed the interrupt */
    while (chr_has_data()) {
      int c = chr_read();
      if (c >= 0) tty_input(tty, (char)c);
    }
    spin_lock(&tty->lock);

    spin_lock(&ptable_lock);
    int sig = cur->sig_pending;
    spin_unlock(&ptable_lock);
    if (sig & SIGINT) {
      tty->reader = NULL;
      spin_unlock(&tty->lock);
      return -1;
    }
  }

  tty->reader = NULL;

  if (tty->eof) {
    tty->eof = 0;
    spin_unlock(&tty->lock);
    return 0;
  }

  int m = tty->line_len < n ? tty->line_len : n;
  for (int i = 0; i < m; i++)
    ubuf[i] = tty->line_buf[i];

  tty->line_len -= m;
  for (int i = 0; i < tty->line_len; i++)
    tty->line_buf[i] = tty->line_buf[i + m];

  spin_unlock(&tty->lock);
  return m;
}
