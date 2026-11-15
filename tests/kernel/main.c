#include "ktest.h"
#include <diag.h>
void
testmain(Hvs *space)
{
  UNUSED(space);
#if BOOT_CASE == 23
  syscalltest();
  putstr("READY syscalls-restored\n");
#elif BOOT_CASE == 22
  preptest();
  putstr("READY prep-restored\n");
#elif BOOT_CASE == 21
  proctest();
  putstr("READY procs-restored\n");
#elif BOOT_CASE == 20
  lifetest();
  putstr("READY life-restored\n");
#elif BOOT_CASE == 19
  localtest();
  putstr("READY locals-restored\n");
#elif BOOT_CASE == 18
  stacktest(space);
  putstr("READY stacks-restored\n");
#elif BOOT_CASE == 17
  switchtest(space);
  putstr("READY switch-restored\n");
#elif BOOT_CASE == 16
  usertest(space);
#elif BOOT_CASE == 15
  sealtest(space);
  putstr("READY seal-restored\n");
#elif BOOT_CASE == 14
  memtest(space);
  putstr("READY memory-restored\n");
#elif BOOT_CASE == 13
  spacetest(space);
  putstr("READY spaces-restored\n");
#elif BOOT_CASE == 12
  accttest();
  putstr("READY budget-restored\n");
#elif BOOT_CASE >= 4
  vmtest(space);
  panic("VM test returned");
#elif BOOT_CASE == 3
  pgtest();
  putstr("READY pages-restored\n");
#else
  arch_probe();
#if BOOT_CASE == 1
  putstr("READY registers-restored\n");
#else
  panic("fault returned");
#endif
#endif
}
