#include <clock.h>
#include <hal/clock.h>
#include <abi/time.h>
#include <abi/base.h>
#define NS 1000000000UL
static u64 origin, frequency;

/* floor(part * NS / frequency), without overflowing even at u64 frequencies. */
static u64
fraction(u64 part)
{
  u64 remainder = 0, quotient = 0, bit;
  for (bit = 1UL << 29; bit; bit >>= 1) {
    quotient <<= 1;
    if (remainder >= frequency - remainder) {
      remainder -= frequency - remainder;
      quotient++;
    } else
      remainder += remainder;
    if (NS & bit) {
      if (remainder >= frequency - part) {
        remainder -= frequency - part;
        quotient++;
      } else
        remainder += part;
    }
  }
  return quotient;
}
int
clockinit(void)
{
  frequency = hal_clock_freq();
  if (!frequency)
    return -ERR_INVALID;
  origin = hal_clock_read();
  return 0;
}
int
clocktime(u64 id, isize *seconds, u64 *nanos)
{
  u64 elapsed, whole;
  if (id != CLOCK_MONOTONIC && id != CLOCK_REALTIME)
    return -ERR_INVALID;
  if (!frequency)
    return -ERR_NOTSUP;
  elapsed = hal_clock_read() - origin;
  whole = elapsed / frequency;
  if (whole > 0x7fffffffffffffffUL)
    return -ERR_OVERFLOW;
  *seconds = (isize) whole;
  *nanos = fraction(elapsed % frequency);
  return 0;
}
int
clockres(u64 id, isize *seconds, u64 *nanos)
{
  u64 resolution;
  if (id != CLOCK_MONOTONIC && id != CLOCK_REALTIME)
    return -ERR_INVALID;
  if (!frequency)
    return -ERR_NOTSUP;
  resolution = NS / frequency + (NS % frequency != 0);
  *seconds = (isize) (resolution / NS);
  *nanos = resolution % NS;
  return 0;
}
int
clockmono(u64 *nanos)
{
  isize seconds;
  u64   fractionns;
  int   error = clocktime(CLOCK_MONOTONIC, &seconds, &fractionns);
  if (error)
    return error;
  if ((u64) seconds > (~0UL - fractionns) / NS)
    return -ERR_OVERFLOW;
  *nanos = (u64) seconds * NS + fractionns;
  return 0;
}
