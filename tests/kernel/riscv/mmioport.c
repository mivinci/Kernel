#include <types.h>
#include <abi/system.h>
#include "private/io.h"
u32
mmiokind(void)
{
  return BOOT_UART_NS16550;
}
u32
mmioready(u64 base)
{
  u32 value = ioread8(base + 5);
  return (value & 0x20) != 0;
}
u32
mmioclear(u64 base)
{
  iowrite8(base + 1, 0);
  return ioread8(base + 1) == 0;
}
