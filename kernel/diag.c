/* Polling diagnostics; available after the architecture console is ready. */
#include <diag.h>
#include <hal/boot.h>

void
putstr(const char *text)
{
  while (*text != 0) {
    hal_debug_putc(*text++);
  }
}

void
puthex(u64 value)
{
  int shift;
  for (shift = 60; shift >= 0; shift -= 4) {
    hal_debug_putc("0123456789abcdef"[(value >> shift) & 15]);
  }
}

NORETURN void
panic(const char *reason)
{
  putstr("PANIC ");
  putstr(reason);
  putstr("\n");
  hal_halt();
}
