#include <types.h>
#include <abi/system.h>
#include "private/io.h"
u32
mmiokind(void)
{
  return BOOT_UART_PL011;
}
u32
mmioready(u64 base)
{
  u32 value = ioread32(base + 0x18);
  return (value & 0x80) != 0;
}
u32
mmioclear(u64 base)
{
  iowrite32(base + 0x38, 0);
  iowrite32(base + 0x44, 0x7ff);
  return ioread32(base + 0x38) == 0;
}
