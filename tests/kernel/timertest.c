#include "ktest.h"
#include <proc.h>
#include <prep.h>
#include <abi/boot.h>
#include <clock.h>
#include <diag.h>
#include <vm.h>
extern const u8 timer_begin[], timer_end[];
#define REQUIRE(c)                                                                                 \
  do {                                                                                             \
    if (!(c)) {                                                                                    \
      putstr("TIMERFAIL line=");                                                                   \
      puthex(__LINE__);                                                                            \
      putstr("\n");                                                                                \
      panic("timer preemption");                                                                   \
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
static Memid
load(Handle pid, u64 entry)
{
  Rtable *t = proctable(pid);
  Handle  allocator = resource(t, LOCAL_ALLOC), space = resource(t, LOCAL_SPACE), code, stack, boot,
         flag;
  Allocarg args;
  Config   c;
  Boot     header;
  u64      resources[8];
  u32      i;
  Memid    flagid;
  Local   *slot;
  args.bytes = PAGE_SIZE;
  args.kind = ALLOC_CODE;
  args.align = args.minaddr = args.maxaddr = args.reserved = 0;
  REQUIRE(!localalloc(t, allocator, &args, &code));
  REQUIRE((u64) (timer_end - timer_begin) <= PAGE_SIZE);
  REQUIRE(!localwrite(t, code, 0, timer_begin, (u64) (timer_end - timer_begin)) &&
          !localseal(t, code));
  args.bytes = 65536;
  args.kind = ALLOC_DATA;
  REQUIRE(!localalloc(t, allocator, &args, &stack));
  args.bytes = PAGE_SIZE;
  REQUIRE(!localalloc(t, allocator, &args, &boot));
  for (i = 0; i < sizeof(header); i++)
    ((u8 *) &header)[i] = 0;
  header.magic = BOOT_MAGIC;
  header.format = BOOT_FORMAT;
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
  REQUIRE(!localalloc(t, allocator, &args, &flag));
  REQUIRE(!localmap(t, space, flag, 0, 0x40000, PAGE_SIZE, VM_READ | VM_WRITE));
  slot = &t->slots[handleindex(flag)];
  flagid.generation = slot->objectgen;
  flagid.index = slot->object;
  flagid.reserved = 0;
  c.entry = entry;
  c.stack = 0x3fffff0000UL;
  c.boot = 0x3fff000000UL;
  c.bootsz = header.bytes;
  c.sigmask = c.sigdefault = 0;
  REQUIRE(!procconfigure(pid, &c, 0));
  return flagid;
}

void
timertest(void)
{
  Setid      set;
  Set        group;
  Handle     pids[2], child, construct, memory;
  Procinfo   info, target;
  Lifeid     lives[2];
  Memid      flags[2];
  Rtable    *source;
  Prepid     prep;
  Prepinfo   scope;
  Liferesult result;
  Acct       account;
  Life       life;
  Allocarg   args;
  Pgstat     before, after;
  u64        now, deadline, one = 1, previous = 0, turns = 0;
  u32        i, expired = 0, seen[2];
  int        error;
  pgstat(&before);
  REQUIRE(!setcreate(4096, 8192, 8, &set));
  for (i = 0; i < 2; i++) {
    REQUIRE(!procnew(set, 1, 256, 512, LOCALS, &pids[i]) && !procinfo(pids[i], &info));
    lives[i] = info.life;
    flags[i] = load(pids[i], 0x10000 + (u64) i * 0x10000);
    seen[i] = 0;
  }
  source = proctable(pids[0]);
  REQUIRE(source && !procnew(set, pids[0], 256, 512, LOCALS, &child));
  REQUIRE(!procinfo(child, &target) && !procprepare(source, child, 31, &construct));
  REQUIRE(!localtarget(source, construct, PREP_ALLOC, &prep, &info.pid));
  args.bytes = PAGE_SIZE;
  args.kind = ALLOC_DATA;
  args.align = args.minaddr = args.maxaddr = args.reserved = 0;
  REQUIRE(!localalloc(source, construct, &args, &memory));
  REQUIRE(!localmap(source, resource(source, LOCAL_SPACE), memory, 0, 0x50000, PAGE_SIZE,
                    VM_READ | VM_WRITE));
  REQUIRE(!prepstat(prep, &scope));
  deadline = scope.deadline;
  REQUIRE(!procpublish(pids[0]) && !procpublish(pids[1]) && !proctimerstart());
  /* No retained source-table borrow across publication or dispatch. */
  source = NULL;
  while (!(error = procrunone())) {
    REQUIRE(++turns < 10000 && !clockmono(&now) && now >= previous);
    previous = now;
    for (i = 0; i < 2; i++) {
      REQUIRE(!procinfo(pids[i], &info));
      if (info.state == PROC_READY)
        seen[i]++;
    }
    if (!expired && !prepstat(prep, &scope) && scope.expired) {
      REQUIRE(now >= deadline && scope.state == PREP_ABORTED && scope.temporary == 1);
      REQUIRE(procticks() >= 4 && seen[0] >= 2 && seen[1] >= 2);
      for (i = 0; i < 2; i++) {
        u64 pa;
        REQUIRE(!mempage(flags[i], 0, &pa));
        REQUIRE(*(volatile u64 *) ((u8 *) kaddr(pa) + 8) > 100);
      }
      for (i = 0; i < 2048; i++) {
        REQUIRE(procwork(1) >= 0 && prepwork(1) >= 0);
      }
      REQUIRE(procinfo(child, &info) == -ERR_STALE);
      REQUIRE(!acctstat(target.acct, &account) && account.state == AC_CLOSING && account.used == 1);
      REQUIRE(!lifestat(target.life, &life) && life.cleanup == 1 && life.run == PG_NONE);
      expired = 1;
      for (i = 0; i < 2; i++)
        REQUIRE(!memwrite(flags[i], 0, &one, sizeof(one)));
    }
    REQUIRE(now < deadline + 5000000000UL);
  }
  REQUIRE(error == -ERR_WOULD_BLOCK && expired);
  for (i = 0; i < 2048; i++)
    REQUIRE(procwork(1) >= 0 && prepwork(1) >= 0);
  for (i = 0; i < 2; i++)
    REQUIRE(!lifecollect(lives[i], 1, &result) && result.reason == LIFE_EXIT && result.code == 42);
  REQUIRE(acctstat(target.acct, &account) == AC_BAD && lifestat(target.life, &life) == -ERR_STALE);
  REQUIRE(!setstat(set, &group) && !group.charged && !group.instances && !setclose(set));
  pgstat(&after);
  REQUIRE(before.allocated == after.allocated && before.free == after.free && !pgcheck() &&
          !acctcheck());
  putstr("TIMERCHECK ticks=");
  puthex(procticks());
  putstr(" turns=");
  puthex(turns);
  putstr(" expired=");
  puthex(expired);
  putstr(" retained=");
  puthex(1);
  putstr(" exits=");
  puthex(2);
  putstr(" charged=");
  puthex(group.charged);
  putstr(" allocated=");
  puthex(after.allocated);
  putstr("\n");
}
