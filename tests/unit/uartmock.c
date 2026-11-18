#include <types.h>
#include "private/io.h"
typedef u32 (*Mockread)(u64, u32);
typedef void (*Mockwrite)(u64, u32, u32);
static Mockread  reader;
static Mockwrite writer;
void
uartmockio(Mockread read, Mockwrite write)
{
  reader = read;
  writer = write;
}
u32
ioread8(u64 address)
{
  return reader(address, 1);
}
u32
ioread32(u64 address)
{
  return reader(address, 4);
}
void
iowrite8(u64 address, u32 value)
{
  writer(address, value & 255, 1);
}
void
iowrite32(u64 address, u32 value)
{
  writer(address, value, 4);
}
