#include "../private/mmio.h"

void
virtiofence(void *context, u32 phase)
{
  UNUSED(context);
  UNUSED(phase);
  __asm__ __volatile__("fence iorw, iorw" ::: "memory");
}
