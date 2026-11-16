/* Private entry points; never included by a normal kernel build. */
#ifndef _KTEST_H
#define _KTEST_H
#include <hal/vm.h>
void testmain(Hvs *space);
void pgtest(void);
void vmtest(Hvs *space);
void arch_probe(void);
void arch_vmfault(u64 address);
void accttest(void);
void spacetest(Hvs *kernel);
void syscalltest(void);
void timertest(void);
void irqtest(void);
void imagetest(void);
void rqtest(void);
void ipctest(void);
void credtest(void);
void terminatetest(void);
void grouptest(void);
void statstest(void);
void createtest(void);
void controltest(void);
void eventtest(void);
void preptest(void);
#if BOOT_CASE == 21 || BOOT_CASE == 22
#include <hal/context.h>
NORETURN void proctesttrap(Hctx *context);
NORETURN void preptesttrap(Hctx *context);
#endif
void proctest(void);
#if BOOT_CASE == 16
#include <hal/context.h>
NORETURN void usertest(Hvs *kernel);
NORETURN void usertesttrap(Hctx *context);
void          arch_usercheck(Hctx *context, u32 phase);
#endif
void lifetest(void);
void localtest(void);
void stacktest(Hvs *kernel);
void switchtest(Hvs *kernel);
void sealtest(Hvs *kernel);
void memtest(Hvs *kernel);
#endif
