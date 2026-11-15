/*
 * RISC-V 64 早期控制台：QEMU virt 上的 NS16550A。
 *
 * 物理地址 0x10000000，IRQ 10。这里只做轮询输出，输入和中断
 * 由用户态串口服务器接管。
 */
#include <hal/boot.h>
#include <types.h>

static u64 uartbase = 0x10000000UL;

#define UART_RBR 0x00 /* 读：接收缓冲 */
#define UART_THR 0x00 /* 写：发送保持 */
#define UART_IER 0x01
#define UART_FCR 0x02
#define UART_LCR 0x03
#define UART_LSR 0x05

#define LSR_THRE 0x20 /* 发送保持寄存器空 */

INLINE u8
uartread(u32 off)
{
  return *(volatile u8 *) (uartbase + off);
}

INLINE void
uartwrite(u32 off, u8 val)
{
  *(volatile u8 *) (uartbase + off) = val;
}

void
hal_debug_putc(u8 c)
{
  while ((uartread(UART_LSR) & LSR_THRE) == 0) {
    /* 等发送缓冲空。QEMU 上几乎立即返回。 */
  }
  uartwrite(UART_THR, (u8) c);
}

void
hal_debug_rebase(u64 address)
{
  uartbase = address;
}
