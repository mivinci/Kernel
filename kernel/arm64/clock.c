#include <hal/clock.h>
void
hal_clock_init(u64 timebase)
{
  UNUSED(timebase);
}
u64
hal_clock_read(void)
{
  u64 value;
  __asm__ __volatile__("isb; mrs %0, cntvct_el0" : "=r"(value)::"memory");
  return value;
}
u64
hal_clock_freq(void)
{
  u64 value;
  __asm__ __volatile__("mrs %0, cntfrq_el0" : "=r"(value));
  return value;
}
