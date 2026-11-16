#include <types.h>
u32
mmioready(u64 base)
{
  u32 value = *(volatile u32 *) (base + 0x18);
  __asm__ __volatile__("dsb sy" ::: "memory");
  return (value & 0x80) != 0;
}
u32
mmioclear(u64 base)
{
  *(volatile u32 *) (base + 0x38) = 0;
  *(volatile u32 *) (base + 0x44) = 0x7ff;
  __asm__ __volatile__("dsb sy; isb" ::: "memory");
  return *(volatile u32 *) (base + 0x38) == 0;
}
