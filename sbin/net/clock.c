#include <lwip/sys.h>
#include <stdlib.h>
#include <time.h>

u32_t
sys_now(void)
{
  struct timespec now;
  if (clock_gettime(CLOCK_MONOTONIC, &now))
    abort();
  /* lwIP compares 32-bit millisecond timestamps across wraparound. */
  return (u32_t) now.tv_sec * 1000U + (u32_t) (now.tv_nsec / 1000000L);
}
