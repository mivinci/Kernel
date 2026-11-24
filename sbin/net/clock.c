#include <lwip/sys.h>
#include "private/timer.h"

u32_t
sys_now(void)
{
  u64 now;
  if (systimernow(&now))
    __builtin_trap();
  /* lwIP compares 32-bit millisecond timestamps across wraparound. */
  return (u32_t) (now / 1000000U);
}
