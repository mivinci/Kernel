#include <chr.h>
#include <kernel.h>
#include <types.h>
#include <tty.h>
#include <uart.h>
#include <spinlock.h>

unsigned long uart_mmio_base = 0x10000000UL;

/*
 * Serialize RX register access across harts.  TX (putc/chr_write) is
 * intentionally left unlocked — adding a TX lock without disabling
 * interrupts would deadlock when printk is called from trap context
 * and the interrupt handler tries to write to the same lock.
 */
static SpinLock uart_rx_lock;

/* ── ChrOps backing (called by chr_read / chr_write / chr_has_data) ── */

static int uart_chr_read(void) {
  spin_lock(&uart_rx_lock);
  if (READ(LSR) & LSR_RX_READY) {
    int c = (unsigned char)READ(RBR);
    spin_unlock(&uart_rx_lock);
    return c;
  }
  spin_unlock(&uart_rx_lock);
  return -1;
}

static void uart_chr_write(int c) {
  while (!(READ(LSR) & LSR_TX_READY))
    ;
  WRITE(THR, c);
}

static int uart_chr_has_data(void) {
  spin_lock(&uart_rx_lock);
  int rd = (READ(LSR) & LSR_RX_READY) != 0;
  spin_unlock(&uart_rx_lock);
  return rd;
}

static ChrOps uart_chr_ops = {
    .read     = uart_chr_read,
    .write    = uart_chr_write,
    .has_data = uart_chr_has_data,
    .init     = NULL,
};

/* ── Hardware init ── */

void uart_init(void) {
  spin_init(&uart_rx_lock);
  WRITE(IER, 0);
  WRITE(LCR, LCR_DLAB);
  WRITE(DLL, 0x01);
  WRITE(DLM, 0x00);
  WRITE(LCR, LCR_8BIT);
  WRITE(FCR, FCR_ENABLE);
  WRITE(MCR, MCR_OUT2);
  WRITE(IER, IER_RDA);
  chr_set_console(&uart_chr_ops);
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
  /* Buffer bytes under uart_rx_lock, then deliver outside the lock
   * to avoid lock-ordering deadlock with tty->lock → chr_write. */
  char buf[16];
  int  n = 0;
  spin_lock(&uart_rx_lock);
  while (n < 16 && (READ(LSR) & LSR_RX_READY))
    buf[n++] = READ(RBR);
  spin_unlock(&uart_rx_lock);
  for (int i = 0; i < n; i++)
    tty_input(&console_tty, buf[i]);
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
