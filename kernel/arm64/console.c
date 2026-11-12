/*
 * ARM64 早期控制台：QEMU virt 上的 PL011 UART。
 *
 * 物理地址 0x09000000。这里只做轮询输出；输入与中断
 * 由用户态驱动接管（与 RISC-V 侧同样的划分）。
 */
#include <console.h>
#include <types.h>

#define PL011_BASE 0x09000000UL

#define PL011_DR   0x00
#define PL011_FR   0x18
#define PL011_IBRD 0x24
#define PL011_FBRD 0x28
#define PL011_LCRH 0x2C
#define PL011_CR   0x30

#define FR_TXFF (1 << 5) /* 发送 FIFO 满 */

static inline u32
plread(u32 off)
{
  return *(volatile u32 *) (PL011_BASE + off);
}

static inline void
plwrite(u32 off, u32 val)
{
  *(volatile u32 *) (PL011_BASE + off) = val;
}

/*
 * PL011 初始化。
 *
 * 必须有这一步：QEMU 报 "data written to disabled UART" 就是写了
 * 但没使能。CR.UARTEN (bit 0) 与 CR.TXE (bit 8) 都要置位。
 *
 * 波特率在 QEMU 上不影响功能，但真实硬件需要正确设置；
 * 这里按 115200 计算，24 MHz 参考时钟。
 */

void
arch_console_init(void)
{
  plwrite(PL011_CR, 0); /* 先关掉再配置 */

  plwrite(PL011_IBRD, 13); /* 24000000 / (16 * 115200) = 13.02 */
  plwrite(PL011_FBRD, 1);  /* 小数部分 .02 * 64 ≈ 1 */

  /* 8 位、无校验、1 停止位、FIFO 使能 */
  plwrite(PL011_LCRH, (3 << 5) | (1 << 4));

  plwrite(PL011_CR, (1 << 0) | (1 << 8) | (1 << 9)); /* UARTEN|TXE|RXE */
}

void
arch_console_putc(char c)
{
  while (plread(PL011_FR) & FR_TXFF) {
    /* 等 FIFO 有空间 */
  }
  plwrite(PL011_DR, (u32) (u8) c);
}

void
arch_console_write(const char *s, size_t n)
{
  size_t i;
  for (i = 0; i < n; i++) {
    if (s[i] == '\n') {
      arch_console_putc('\r');
    }
    arch_console_putc(s[i]);
  }
}
