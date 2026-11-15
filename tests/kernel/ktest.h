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
#if BOOT_CASE == 16
#include <hal/context.h>
NORETURN void usertest(Hvs *kernel);
NORETURN void usertesttrap(Hctx *context);
void          arch_usercheck(Hctx *context, u32 phase);
#endif
void switchtest(Hvs *kernel);
void sealtest(Hvs *kernel);
void memtest(Hvs *kernel);
#endif
