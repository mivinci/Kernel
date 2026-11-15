#include <hal/vm.h>
void
hal_code_sync(u64 pa, u64 bytes)
{
  UNUSED(pa);
  UNUSED(bytes);
  __asm__ __volatile__("fence.i" ::: "memory");
}
