#include <types.h>
u32
transportprobe(u64 base)
{
  u32 magic = *(volatile u32 *) base, version = *(volatile u32 *) (base + 4);
  __asm__ __volatile__("fence iorw, iorw" ::: "memory");
  return magic == 0x74726976U ? version : 0;
}
