#include <chr.h>
#include <kernel.h>
#include <types.h>
#include <tty.h>
#include <uart.h>
#include <spinlock.h>
#include <proc.h>

unsigned long uart_mmio_base = 0x10000000UL;

/* ── RX lock ── */
static SpinLock uart_rx_lock;

/* ── Interrupt-driven TX ring buffer ──
 *
 * uart_chr_write (user-process output via chr_write / sys_write)
 * pushes bytes into the ring and enables THRE.  The THRE ISR in
 * uart_handle pops bytes one at a time and writes them to THR.
 *
 * putc (kernel messages via printk) stays as a direct THR write
 * — it is never called from process context in a tight loop,
 * so the simpler busy-wait is safe for kernel messages.
 */
#define TX_BUF_SIZE 128
static char      tx_buf[TX_BUF_SIZE];
static int       tx_w;        /* producer index */
static int       tx_r;        /* consumer index */
static int       tx_count;    /* cached count */
static SpinLock  tx_lock;

static int tx_empty(void) { return tx_count == 0; }
static int tx_full(void)  { return tx_count == TX_BUF_SIZE; }

static void tx_thre_enable(void)  { *(volatile char *)(uart_mmio_base + IER) |= IER_THRE; }
static void tx_thre_disable(void) { *(volatile char *)(uart_mmio_base + IER) &= ~IER_THRE; }

static int tx_push(char c) {
  if (tx_full()) return -1;
  tx_buf[tx_w] = c;
  tx_w = (tx_w + 1) % TX_BUF_SIZE;
  tx_count++;
  return 0;
}

static int tx_pop(void) {
  if (tx_empty()) return -1;
  char c = tx_buf[tx_r];
  tx_r = (tx_r + 1) % TX_BUF_SIZE;
  tx_count--;
  return (unsigned char)c;
}

/* ── UART interrupt handler (called from PLIC) ── */

void uart_handle(void) {
  char rxb[16];
  int  rxn = 0;

  spin_lock(&uart_rx_lock);

  /* Drain RX */
  while (rxn < 16 && (READ(LSR) & LSR_RX_READY))
    rxb[rxn++] = READ(RBR);

  /* IIR loop: handle both RX and TX in the same claim */
  for (;;) {
    unsigned char iir = READ(IIR);
    if (iir & IIR_NO_PEND) break;
    unsigned char src = iir & 0x0e;
    if (src == IIR_TX) {
      /* TX handler: pop from ring, write to THR */
      spin_lock(&tx_lock);
      int c = tx_pop();
      if (c < 0) {
        tx_thre_disable();
        if (!tx_empty()) tx_thre_enable();  /* double-check */
      } else {
        WRITE(THR, (char)c);
      }
      spin_unlock(&tx_lock);
    } else {
      /* RX or timeout — already drained above, skip */
      break;
    }
  }

  spin_unlock(&uart_rx_lock);

  /* Deliver RX bytes to TTY outside the lock */
  for (int i = 0; i < rxn; i++)
    tty_input(&console_tty, rxb[i]);
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

/*
 * Buffered write for user-process output (via chr_write / sys_write).
 * Pushes into the interrupt-driven ring; the THRE ISR drains.
 * If the ring is full (should be rare with 128 slots) we fall
 * back to a direct THR write to avoid blocking the caller.
 */
static void uart_chr_write(int c) {
  spin_lock(&tx_lock);
  if (!tx_full()) {
    int was_empty = tx_empty();
    tx_push((char)c);
    if (was_empty) tx_thre_enable();
    spin_unlock(&tx_lock);
  } else {
    /* Ring full — fall back to direct write */
    spin_unlock(&tx_lock);
    while (!(READ(LSR) & LSR_TX_READY))
      ;
    WRITE(THR, c);
  }
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
  spin_init(&tx_lock);
  tx_w = tx_r = tx_count = 0;

  WRITE(IER, 0);
  WRITE(LCR, LCR_DLAB);
  WRITE(DLL, 0x01);
  WRITE(DLM, 0x00);
  WRITE(LCR, LCR_8BIT);
  WRITE(FCR, FCR_ENABLE);
  WRITE(MCR, MCR_OUT2);
  WRITE(IER, IER_RDA | IER_THRE);  /* RX + TX interrupts */
  chr_set_console(&uart_chr_ops);
}

/*
 * putc — kernel messages (printk).  Direct THR write with
 * unconditional busy-wait.  Never called from a tight process
 * loop, so the simpler busy-wait is safe.
 */
void putc(char c) {
  while (!(READ(LSR) & LSR_TX_READY))
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

int printk(const char *fmt, ...) {
  va_list args;
  char    buf[1024];
  va_start(args, fmt);
  vsprintf(buf, fmt, args);
  va_end(args);
  puts(buf);
  return 0;
}
