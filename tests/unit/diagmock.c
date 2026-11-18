#include <hal/boot.h>
#include <hal/irq.h>
#include <clock.h>
static u8  output[4096];
static u64 count, masked, now, available, violations;
int
hal_debug_handoff(void)
{
  return 0;
}
int
hal_debug_panic(void)
{
  return 0;
}
void
diagmocktime(u64 time, u64 valid)
{
  now = time;
  available = valid;
}
u64
diagmockoutput(u8 *out, u64 bytes)
{
  u64 i;
  if (bytes < count)
    return 0;
  for (i = 0; i < count; i++)
    out[i] = output[i];
  return count;
}
u64
diagmockviolations(void)
{
  return violations + masked;
}
u64
hal_irq_save(void)
{
  u64 old = masked;
  masked = 1;
  return old;
}
void
hal_irq_restore(u64 old)
{
  masked = old;
}
void
hal_debug_putc(u8 byte)
{
  if (!masked)
    violations++;
  if (count < sizeof(output))
    output[count++] = byte;
}
int
clockmono(u64 *out)
{
  if (!masked)
    violations++;
  *out = available ? now : 0;
  return available ? 0 : -1;
}
NORETURN void
hal_halt(void)
{
  __builtin_trap();
}
