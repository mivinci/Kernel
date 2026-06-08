#ifndef _UART_H
#define _UART_H

/* Default UART base (QEMU virt) — overridden by FDT at runtime */
extern unsigned long uart_mmio_base;

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
#define IER_RDA  0x01 /* Received Data Available */
#define IER_THRE 0x02 /* Transmit Holding Register Empty */

/* Interrupt Identification Register (read-only) */
#define IIR         0x02
#define IIR_NO_PEND 0x01 /* No interrupt pending */
#define IIR_TX      0x02 /* THRE interrupt */
#define IIR_RX      0x04 /* RDA interrupt */
#define IIR_RX_TO   0x0c /* RDA timeout */

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
#define READ(reg)       (*(volatile char *)(uart_mmio_base + (reg)))
#define WRITE(reg, val) (*(volatile char *)(uart_mmio_base + (reg))) = (val)

void putc(char c);
void puts(char *p);
void uart_init(void);
void uart_handle(void);

#endif /* _UART_H */
