#include <kernel.h>
#include <types.h>
#include <proc.h>
#include <spinlock.h>
#include <tty.h>
#include <uart.h>

Tty console_tty;

static void tty_wake_reader(Tty *tty) {
  if (!tty->reader) return;
  spin_lock(&ptable_lock);
  if (tty->reader->state == IOWAIT)
    tty->reader->state = RUNNABLE;
  spin_unlock(&ptable_lock);
}

static void tty_echo_erase(void) {
  putc('\b'); putc(' '); putc('\b');
}

static void tty_echo_erase_line(Tty *tty) {
  for (int i = 0; i < tty->line_len; i++)
    tty_echo_erase();
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
  spin_lock(&tty->lock);

  switch ((unsigned char)c) {
  case TTY_CTRL_C:
    spin_unlock(&tty->lock);
    tty_send_sigint(tty);
    spin_lock(&tty->lock);
    tty->eof = 1;
    tty_wake_reader(tty);
    break;
  case TTY_CTRL_D:
    if (tty->line_len == 0) {
      tty->eof = 1;
      tty_wake_reader(tty);
    }
    break;
  case TTY_BS:
    if (tty->line_len > 0) {
      tty->line_len--;
      if (tty->echo) tty_echo_erase();
    }
    break;
  case TTY_CTRL_U:
    if (tty->echo) tty_echo_erase_line(tty);
    tty->line_len = 0;
    break;
  case '\r':
    c = '\n';
    /* fall through */
  case '\n':
    if (tty->line_len < TTY_LINE_MAX - 1)
      tty->line_buf[tty->line_len++] = '\n';
    if (tty->echo) { putc('\r'); putc('\n'); }
    tty_wake_reader(tty);
    break;
  default:
    if (c >= ' ' && tty->line_len < TTY_LINE_MAX - 1) {
      tty->line_buf[tty->line_len++] = c;
      if (tty->echo) putc(c);
    }
    break;
  }

  spin_unlock(&tty->lock);
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
    /* Poll UART for chars that missed the interrupt */
    spin_unlock(&tty->lock);
    yield();
    /* Poll UART for chars that missed the interrupt */
    while (READ(LSR) & LSR_RX_READY)
      tty_input(tty, READ(RBR));
    spin_lock(&tty->lock);
    spin_lock(&tty->lock);

    if (cur->sig_pending & SIGINT) {
      spin_unlock(&tty->lock);
      spin_lock(&ptable_lock);
      cur->state    = ZOMBIE;
      cur->exitcode = -1;
      spin_unlock(&ptable_lock);
      yield();
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
