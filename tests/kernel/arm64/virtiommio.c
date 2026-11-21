#include "../virtiouser.h"
#include <abi/base.h>
static void
barrier(void)
{
  __asm__ __volatile__("dsb sy" ::: "memory");
}
void
virtiofence(void *context, u32 phase)
{
  UNUSED(context);
  UNUSED(phase);
  barrier();
}
int
virtioread(void *context, u32 offset, u32 *out)
{
  u64 base = *(u64 *) context;
  if (!out || offset > 0x114 || (offset & 3))
    return -ERR_INVALID;
  barrier();
  *out = *(volatile u32 *) (base + offset);
  barrier();
  return 0;
}
int
virtiowrite(void *context, u32 offset, u32 value)
{
  u64 base = *(u64 *) context;
  if (offset > 0xfc || (offset & 3))
    return -ERR_INVALID;
  barrier();
  *(volatile u32 *) (base + offset) = value;
  barrier();
  return 0;
}
