#ifndef _UART_H
#define _UART_H

/* NS16550-compatible UART base address */
#define UART_BASE 0x10000000L

/* Register offsets */
#define THR 0x00 /* Transmit Holding Register (DLAB=0) */
#define RBR 0x00 /* Receive Buffer Register (DLAB=0) */
#define DLL 0x00 /* Divisor Latch LSB (DLAB=1) */
#define IER 0x01 /* Interrupt Enable Register (DLAB=0) */
#define DLM 0x01 /* Divisor Latch MSB (DLAB=1) */
#define FCR 0x02 /* FIFO Control Register */
#define LCR 0x03 /* Line Control Register */
#define MCR 0x04 /* Modem Control Register */
#define LSR 0x05 /* Line Status Register */

/* Interrupt Enable Register bits */
#define IER_RDA 0x01 /* Received Data Available */

/* Line Control Register bits */
#define LCR_DLAB 0x80 /* Divisor Latch Access Bit */
#define LCR_8BIT 0x03 /* 8 data bits */

/* FIFO Control Register bits */
#define FCR_ENABLE 0x01 /* Enable FIFO */

/* Modem Control Register bits */
#define MCR_OUT2 0x08 /* Enable interrupt output */

/* Line Status Register bits */
#define LSR_TX_READY 0x20 /* Transmitter buffer empty */
#define LSR_RX_READY 0x01 /* Data ready */

/* MMIO read/write macros */
#define READ(reg)       (*(volatile char *)(UART_BASE + (reg)))
#define WRITE(reg, val) (*(volatile char *)(UART_BASE + (reg))) = (val)

void putc(char c);
void uart_init(void);
void uart_handle(void);
int  getc(void);

#endif /* _UART_H */
