#include "ktest.h"
#include <hal/context.h>
#include <diag.h>
#include <page.h>
#include <vm.h>
extern u64   arch_switchprobe(Hkctx *from, const Hkctx *to);
extern void  arch_switchinit(Hkctx *context, u64 top, void (*entry)(void));
extern u64   arch_stackpointer(void);
static Hkctx caller, worker;
static u32   resumed;
#define STACK_BASE (KIO + 0x600000)
#define REQUIRE(c)                                                                                 \
  do {                                                                                             \
    if (!(c))                                                                                      \
      panic("kernel stack switch");                                                                \
  } while (0)
static NORETURN void
run(void)
{
  volatile u64 local = 0x12345678;
  u64          sp = arch_stackpointer();
  REQUIRE(sp >= STACK_BASE && sp < STACK_BASE + 4 * PAGE_SIZE);
  resumed++;
  hal_ctx_switch(&worker, &caller);
  REQUIRE(arch_stackpointer() == sp && local == 0x12345678);
  resumed++;
  hal_ctx_switch(&worker, &caller);
  panic("discarded kernel context resumed");
}
void
switchtest(Hvs *kernel)
{
  Pgblk  block;
  Pgstat before, after;
  Hpte   pte;
  u32    i;
  pgstat(&before);
  REQUIRE(!pgalloc(2, NULL, &block));
  for (i = 0; i < 4; i++)
    REQUIRE(!hal_vm_map(kernel, STACK_BASE + (u64) i * PAGE_SIZE, block.pa + (u64) i * PAGE_SIZE,
                        VM_READ | VM_WRITE, VM_NORMAL));
  hal_vm_commit(kernel);
  REQUIRE(!hal_vm_lookup(kernel, STACK_BASE - PAGE_SIZE, &pte));
  REQUIRE(!hal_vm_lookup(kernel, STACK_BASE + 4 * PAGE_SIZE, &pte));
  arch_switchinit(&worker, STACK_BASE + 4 * PAGE_SIZE, run);
  REQUIRE(arch_switchprobe(&caller, &worker) && resumed == 1);
  REQUIRE(arch_switchprobe(&caller, &worker) && resumed == 2);
  /* No future continuation owns this test stack after the explicit discard. */
  for (i = 0; i < sizeof(worker); i++)
    ((u8 *) &worker)[i] = 0;
  for (i = 0; i < 4; i++)
    REQUIRE(!hal_vm_unmap(kernel, STACK_BASE + (u64) i * PAGE_SIZE));
  REQUIRE(!pgfree(block));
  pgstat(&after);
  REQUIRE(before.free == after.free && before.allocated == after.allocated && !pgcheck());
  putstr("SWITCHCHECK resumed=");
  puthex(resumed);
  putstr(" allocated=");
  puthex(after.allocated);
  putstr("\n");
}
