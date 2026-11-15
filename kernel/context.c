#include <hal/context.h>
#include <diag.h>
#if BOOT_CASE == 16
#include "ktest.h"
#endif
NORETURN void
usertrap(Hctx *context)
{
#if BOOT_CASE == 16
  usertesttrap(context);
#else
  UNUSED(context);
  panic("unexpected user trap");
#endif
}
