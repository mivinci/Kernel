#define UART_BASE 0x10000000L

#define THR 0x00  // 发送保持寄存器
#define RBR 0x00  // 接收缓冲寄存器  
#define LSR 0x05  // 线路状态寄存器

#define LSR_TX_READY 0x20  /* 发送缓冲区空 */
#define LSR_RX_READY 0x01  /* 接收数据就绪 */

#define READ(reg) (*(volatile char*)(UART_BASE + (reg)))
#define WRITE(reg, val) (*(volatile char*)(UART_BASE + (reg))) = (val)


/* 同步发送一个字符 */
void putc(char c) {
  while ((READ(LSR) & LSR_TX_READY) == 0);
  WRITE(THR, c);
}

/* 同步发送一个字符串 */
void puts(char *p) {
  while (*p) {
    putc(*p++);
    if (*p == '\n')
      putc('\r');
  }
}
