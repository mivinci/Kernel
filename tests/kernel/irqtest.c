#include "ktest.h"
#include <proc.h>
#include <prep.h>
#include <abi/boot.h>
#include <clock.h>
#include <diag.h>
#include <hal/clock.h>
extern const u8 irq_begin[], irq_end[];
extern int      arch_irqprobe(void);
extern int      arch_irqcheck(void);
#define REQUIRE(c)                                                                                 \
  do {                                                                                             \
    if (!(c)) {                                                                                    \
      putstr("IRQFAIL line=");                                                                     \
      puthex(__LINE__);                                                                            \
      putstr("\n");                                                                                \
      panic("kernel IRQ");                                                                         \
    }                                                                                              \
  } while (0)
static Handle
resource(Rtable *t, u32 kind)
{
  u32 i;
  for (i = 0; i < t->count; i++)
    if (t->slots[i].subtype == kind)
      return handlepack(HANDLE_RESOURCE, t->slots[i].generation, i);
  panic("native boot resource");
}
static void
load(Handle pid, u64 entry)
{
  Rtable *t = proctable(pid);
  Handle  allocator = resource(t, LOCAL_ALLOC), space = resource(t, LOCAL_SPACE), code, stack, boot;
  Allocarg args;
  Config   c;
  Boot     header;
  u64      resources[8];
  u32      i;
  args.bytes = PAGE_SIZE;
  args.kind = ALLOC_CODE;
  args.align = args.minaddr = args.maxaddr = args.reserved = 0;
  REQUIRE(!localalloc(t, allocator, &args, &code));
  REQUIRE((u64) (irq_end - irq_begin) <= PAGE_SIZE);
  REQUIRE(!localwrite(t, code, 0, irq_begin, (u64) (irq_end - irq_begin)) && !localseal(t, code));
  args.bytes = 65536;
  args.kind = ALLOC_DATA;
  REQUIRE(!localalloc(t, allocator, &args, &stack));
  args.bytes = PAGE_SIZE;
  REQUIRE(!localalloc(t, allocator, &args, &boot));
  for (i = 0; i < sizeof(header); i++)
    ((u8 *) &header)[i] = 0;
  header.magic = BOOT_MAGIC;
  header.format = 1;
  header.bytes = sizeof(header) + sizeof(resources);
  header.resource_count = 2;
  header.resources_off = sizeof(header);
  header.user_lo = 0x10000;
  header.user_hi = 1UL << 38;
  for (i = 0; i < 8; i++)
    resources[i] = 0;
  resources[0] = 1;
  resources[1] = (u64) space;
  resources[4] = 2;
  resources[5] = (u64) allocator;
  REQUIRE(!localwrite(t, boot, 0, &header, sizeof(header)) &&
          !localwrite(t, boot, sizeof(header), resources, sizeof(resources)));
  REQUIRE(!localmap(t, space, code, 0, entry, PAGE_SIZE, VM_READ | VM_EXEC));
  REQUIRE(!localmap(t, space, stack, 0, 0x3ffffe0000UL, 65536, VM_READ | VM_WRITE));
  REQUIRE(!localmap(t, space, boot, 0, 0x3fff000000UL, PAGE_SIZE, VM_READ));
  c.entry = entry;
  c.stack = 0x3fffff0000UL;
  c.boot = 0x3fff000000UL;
  c.bootsz = header.bytes;
  c.sigmask = c.sigdefault = 0;
  REQUIRE(!procconfigure(pid, &c, 0));
}

void
irqtest(void)
{
  Setid      set;
  Set        group;
  Handle     parent, child, construct, ignored;
  Procinfo   info, target;
  Lifeid     parentlife;
  Rtable    *source;
  Prepid     prep;
  Prepinfo   scope;
  Liferesult result;
  Life       life;
  Acct       account;
  Pgstat     before, after;
  u64        now, begin, deadline, ticks, idle = 0, turns = 0, kernel_before;
  u32        i;
  int        error;
  pgstat(&before);
  REQUIRE(!setcreate(4096, 8192, 8, &set));
  REQUIRE(!procnew(set, 1, 256, 512, LOCALS, &parent) && !procinfo(parent, &info));
  parentlife = info.life;
  load(parent, 0x10000);
  source = proctable(parent);
  REQUIRE(!procnew(set, parent, 256, 512, LOCALS, &child) && !procinfo(child, &target));
  REQUIRE(!procprepare(source, child, 31, &construct));
  REQUIRE(!localtarget(source, construct, PREP_PUBLISH, &prep, &ignored));
  REQUIRE(!prepstat(prep, &scope) && !proctimerstart());
  deadline = scope.deadline;
  /* Let the first IRQ become pending while masked: exercise enable-before-WFI. */
  REQUIRE(!clockmono(&begin));
  do {
    REQUIRE(!clockmono(&now));
  } while (now - begin < 10000000UL);
  REQUIRE(arch_irqprobe() == 42 && prockernelticks() == 1 && procticks() == 1);
  source = NULL;
  while (!scope.expired) {
    REQUIRE(procrunone() == -ERR_WOULD_BLOCK && procidle() == 1);
    REQUIRE(++idle < 10000 && !clockmono(&now) && now < deadline + 5000000000UL);
    REQUIRE(!prepstat(prep, &scope));
  }
  REQUIRE(now >= deadline && scope.state == PREP_ABORTED && !scope.temporary);
  ticks = procticks();
  REQUIRE(ticks == prockernelticks() && ticks >= 4);
  /* Finish cleanup through idle itself, without a parent continuation/worker loop. */
  while (procinfo(child, &info) != -ERR_STALE || prepstat(prep, &scope) != -ERR_STALE) {
    REQUIRE(procidle() == 1 && ++idle < 10000);
    REQUIRE(!clockmono(&now) && now < deadline + 10000000000UL);
  }
  REQUIRE(procinfo(child, &info) == -ERR_STALE);
  REQUIRE(lifestat(target.life, &life) == -ERR_STALE && acctstat(target.acct, &account) == AC_BAD);
  REQUIRE(prepstat(prep, &scope) == -ERR_STALE);
  /* Own native allocation admits kernel IRQs but yields only at a checkpoint. */
  kernel_before = prockernelticks();
  REQUIRE(!procpublish(parent));
  while (!(error = procrunone()))
    REQUIRE(++turns < 10000);
  REQUIRE(error == -ERR_WOULD_BLOCK && prockernelticks() > kernel_before && turns > 1);
  REQUIRE(arch_irqcheck());
  for (i = 0; i < 2048; i++)
    REQUIRE(procwork(1) >= 0 && prepwork(1) >= 0);
  REQUIRE(!lifecollect(parentlife, 1, &result) && result.reason == LIFE_EXIT && result.code == 42);
  REQUIRE(!setstat(set, &group) && !group.charged && !group.instances && !setclose(set));
  pgstat(&after);
  REQUIRE(before.allocated == after.allocated && before.free == after.free && !pgcheck() &&
          !acctcheck());
  putstr("IRQCHECK idle=");
  puthex(idle);
  putstr(" kernel_ticks=");
  puthex(prockernelticks());
  putstr(" op_ticks=");
  puthex(prockernelticks() - kernel_before);
  putstr(" turns=");
  puthex(turns);
  putstr(" expired=");
  puthex(1);
  putstr(" exit=");
  puthex(result.code);
  putstr(" charged=");
  puthex(group.charged);
  putstr(" allocated=");
  puthex(after.allocated);
  putstr("\n");
}
