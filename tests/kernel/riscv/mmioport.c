#include <types.h>
u32
mmioready(u64 base)
{
  u32 value = *(volatile u8 *) (base + 5);
  __asm__ __volatile__("fence iorw, iorw" ::: "memory");
  return (value & 0x20) != 0;
}
u32
mmioclear(u64 base)
{
  *(volatile u8 *) (base + 1) = 0;
  __asm__ __volatile__("fence iorw, iorw" ::: "memory");
  return *(volatile u8 *) (base + 1) == 0;
}
