#include <hal/clock.h>
static u64 frequency;
void
hal_clock_init(u64 timebase)
{
  frequency = timebase;
}
u64
hal_clock_read(void)
{
  u64 value;
  __asm__ __volatile__("rdtime %0" : "=r"(value)::"memory");
  return value;
}
u64
hal_clock_freq(void)
{
  return frequency;
}
