#ifndef _UART_H
#define _UART_H

/* NS16550-compatible UART base address */
#define UART_BASE 0x10000000L

/* Register offsets */
#define THR 0x00 /* Transmit Holding Register */
#define RBR 0x00 /* Receive Buffer Register */
#define LSR 0x05 /* Line Status Register */

/* Line Status Register bits */
#define LSR_TX_READY 0x20 /* Transmitter buffer empty */
#define LSR_RX_READY 0x01 /* Data ready */

/* MMIO read/write macros */
#define READ(reg)  (*(volatile char *)(UART_BASE + (reg)))
#define WRITE(reg, val) (*(volatile char *)(UART_BASE + (reg))) = (val)

#endif /* _UART_H */
