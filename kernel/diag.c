/* Polling diagnostics; available after the architecture console is ready. */
#include <diag.h>
#include <hal/boot.h>
#include <hal/irq.h>
#include <clock.h>
#include <klog.h>
static u8  pending[KLOG_TEXT];
static u32 used, fatal;
static void
flush(void)
{
  u64 nanos = 0;
  u32 flags;
  if (!used)
    return;
  flags = clockmono(&nanos) ? 0 : KLOG_TIME;
  if (!flags)
    nanos = 0;
  /* source=0 is kernel, never a kernel pointer. */
  (void) klogwrite(nanos, 0, fatal ? KLOG_ERROR : KLOG_INFO, flags, pending, used);
  used = 0;
}
static void
byte(u8 value)
{
  pending[used++] = value;
  if (value == '\n' || used == KLOG_TEXT)
    flush();
  hal_debug_putc(value);
}

void
putstr(const char *text)
{
  u64 state = hal_irq_save();
  while (*text != 0) {
    byte((u8) *text++);
  }
  hal_irq_restore(state);
}

void
puthex(u64 value)
{
  int shift;
  u64 state = hal_irq_save();
  for (shift = 60; shift >= 0; shift -= 4) {
    byte((u8) "0123456789abcdef"[(value >> shift) & 15]);
  }
  hal_irq_restore(state);
}

NORETURN void
panic(const char *reason)
{
  (void) hal_irq_save();
  flush();
  fatal = 1;
  putstr("PANIC ");
  putstr(reason);
  putstr("\n");
  hal_halt();
}
