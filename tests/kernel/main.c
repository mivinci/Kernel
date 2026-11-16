#include "ktest.h"
#include <diag.h>
void
testmain(Hvs *space)
{
  UNUSED(space);
#if BOOT_CASE == 44
  sigconfigtest();
  putstr("READY sigconfig-restored\n");
#elif BOOT_CASE == 42 || BOOT_CASE == 43
  inittest();
  putstr("READY init-restored\n");
#elif BOOT_CASE == 41
  elftest();
  putstr("READY elf-restored\n");
#elif BOOT_CASE == 40
  runtimetest();
  putstr("READY runtime-restored\n");
#elif BOOT_CASE == 39
  libsysload();
  putstr("READY libsys-restored\n");
#elif BOOT_CASE == 38
  spawntest();
  putstr("READY spawn-restored\n");
#elif BOOT_CASE == 37
  signaltest();
  putstr("READY sigreserve-restored\n");
#elif BOOT_CASE == 36
  createtest();
  putstr("READY createauth-restored\n");
#elif BOOT_CASE == 35
  statstest();
  putstr("READY stats-restored\n");
#elif BOOT_CASE == 34
  grouptest();
  putstr("READY groups-restored\n");
#elif BOOT_CASE == 33
  credtest();
  putstr("READY creds-restored\n");
#elif BOOT_CASE == 32
  terminatetest();
  putstr("READY terminate-restored\n");
#elif BOOT_CASE == 31
  controltest();
  putstr("READY control-restored\n");
#elif BOOT_CASE == 30
  eventtest();
  putstr("READY events-restored\n");
#elif BOOT_CASE == 28 || BOOT_CASE == 29
  ipctest();
  putstr("READY ipc-restored\n");
#elif BOOT_CASE == 27
  rqtest();
  putstr("READY requests-restored\n");
#elif BOOT_CASE == 26
  imagetest();
  putstr("READY images-restored\n");
#elif BOOT_CASE == 25
  irqtest();
  putstr("READY irq-restored\n");
#elif BOOT_CASE == 24
  timertest();
  putstr("READY timer-restored\n");
#elif BOOT_CASE == 23
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
