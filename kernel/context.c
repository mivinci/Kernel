#include <hal/context.h>
#include <diag.h>
#include <syscall.h>
#include <proc.h>
#if BOOT_CASE == 16 || BOOT_CASE == 21 || BOOT_CASE == 22
#include "ktest.h"
#endif
NORETURN void
usertrap(Hctx *context)
{
  Htrap trap;
  hal_trap_decode(context, &trap);
  if (trap.fromuser && (trap.kind == HT_TIMER || trap.kind == HT_IRQ))
    procinterrupttrap(context);
#if BOOT_CASE == 16
  usertesttrap(context);
#elif BOOT_CASE == 21
  proctesttrap(context);
#elif BOOT_CASE == 22
  preptesttrap(context);
#else
  syscall(context);
#endif
}

void
kerneltrap(Hctx *context)
{
  Htrap trap;
  hal_trap_decode(context, &trap);
  if (trap.fromuser || (trap.kind != HT_TIMER && trap.kind != HT_IRQ))
    panic("kernel IRQ source");
  prockernelirq();
}
