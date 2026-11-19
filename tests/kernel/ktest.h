/* Private entry points; never included by a normal kernel build. */
#ifndef _KTEST_H
#define _KTEST_H
#include <hal/vm.h>
#include <dtb.h>
struct Hctx;
void faultframeenter(struct Hctx *context);
void testmain(Hvs *space, const Dtinfo *hardware, u64 cpu);
void sigsendtest(void);
void cputest(const Dtinfo *hardware, u64 current);
int  testcpustart(u32 method, u64 id);
void bootviewtest(void);
void chiptest(const Dtinfo *hardware);
void bindtest(const Dtinfo *hardware);
void grouppagetest(const Dtinfo *hardware);
void vdevtest(const Dtinfo *hardware);
void heaptest(void);
void cowpagetest(void);
void snapshotest(void);
void nativecowtest(void);
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
void terminaltest(void);
void rqtest(void);
void ipctest(void);
void credtest(void);
void terminatetest(void);
void grouptest(void);
void statstest(void);
void createtest(void);
void signaltest(void);
void forkcontexttest(void);
void spawntest(void);
void libsysload(void);
void runtimetest(void);
void ttytest(void);
void jobtest(void);
void elftest(void);
void inittest(void);
void sigconfigtest(void);
void sigframetest(void);
void sigforktest(void);
void suspendtest(void);
void controltest(void);
void eventtest(void);
void preptest(void);
#if BOOT_CASE == 37
#include <hal/context.h>
void arch_sigcontext(Hctx *context, u32 mode);
#endif
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
void localforktest(void);
void stacktest(Hvs *kernel);
void switchtest(Hvs *kernel);
void sealtest(Hvs *kernel);
void memtest(Hvs *kernel);
void forksnapshottest(void);
void forklifetest(void);
void forkjournaltest(void);
void forkstagetest(void);
void forktreetest(void);
void forkbuildtest(void);
void forkfreezetest(void);
void forkcleantest(void);
void forkcontinuetest(void);
#endif
