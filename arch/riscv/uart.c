#include <chr.h>
#include <kernel.h>
#include <types.h>
#include <tty.h>
#include <uart.h>
#include <spinlock.h>
#include <proc.h>

unsigned long uart_mmio_base = 0x10000000UL;

/* Serialize UART register access across harts */
static SpinLock uart_rx_lock;

/*
 * Best-effort TX: try to write to THR if the transmitter is ready.
 * When it is not (another hart is mid-write) we briefly spin with
 * a hard ceiling to avoid livelock during syscalls (MIE=0 blocks
 * timer preemption).  Bytes that can't be sent within the window
 * are silently dropped.
 *
 * During boot (cpu_proc() == NULL) we spin unconditionally since
 * there is no contention.
 */
static void uart_spin_write(int c) {
  while (!(READ(LSR) & LSR_TX_READY))
    ;
  WRITE(THR, c);
}

/* ── ChrOps backing ── */

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
  uart_spin_write(c);
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

/*
 * UART interrupt handler (called from PLIC).
 *
 * Protected by uart_rx_lock so that only one hart touches the
 * IIR / RBR registers at a time.  The PLIC may route a second
 * UART IRQ to another hart before the first claim completes,
 * so re-entrancy must be handled.
 */
void uart_handle(void) {
  char buf[16];
  int  n = 0;

  spin_lock(&uart_rx_lock);
  while (n < 16 && (READ(LSR) & LSR_RX_READY))
    buf[n++] = READ(RBR);
  spin_unlock(&uart_rx_lock);

  for (int i = 0; i < n; i++)
    tty_input(&console_tty, buf[i]);
}

void putc(char c) {
  uart_spin_write(c);
}

void puts(char *p) {
  char c;
  while ((c = *p++) != '\0') {
    putc(c);
    if (c == '\n') putc('\r');
  }
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
