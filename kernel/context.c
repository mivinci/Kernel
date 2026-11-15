#include <hal/context.h>
#include <diag.h>
#include <syscall.h>
#if BOOT_CASE == 16 || BOOT_CASE == 21 || BOOT_CASE == 22
#include "ktest.h"
#endif
NORETURN void
usertrap(Hctx *context)
{
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
