#include "ktest.h"
#include <hal/context.h>
#include <kstack.h>
#include <space.h>
#include <diag.h>
extern u64      arch_switchprobe(Hkctx *from, const Hkctx *to);
extern u64      arch_stackpointer(void);
static Hkctx    caller, worker;
static Kstackid running, pool[KSTACKS];
static u64      top;
static u32      resumed;
#define REQUIRE(c)                                                                                 \
  do {                                                                                             \
    if (!(c))                                                                                      \
      panic("managed kernel stack");                                                               \
  } while (0)
static NORETURN void
run(void)
{
  volatile u64 local = 0x12345678;
  u64          sp = arch_stackpointer(), used;
  REQUIRE(sp >= top - KSTACK_PAGES * PAGE_SIZE && sp < top);
  REQUIRE(kstackfree(running) == KS_BUSY && kstackdrop(running) == KS_BUSY);
  REQUIRE(kstackused(running, &used) == KS_BUSY);
  resumed++;
  hal_ctx_switch(&worker, &caller);
  REQUIRE(arch_stackpointer() == sp && local == 0x12345678);
  resumed++;
  hal_ctx_switch(&worker, &caller);
  panic("discarded managed stack resumed");
}
void
stacktest(Hvs *kernel)
{
  Setid    set;
  Acctid   acct;
  Spaceid  space;
  Kstackid failed;
  Kstack   record;
  Pgstat   before, current, after;
  Acct     account;
  Hpte     entry;
  u32      i, j;
  u64      pages = kernel->pages, used;
  pgstat(&before);
  REQUIRE(!setcreate(4096, 8192, 8, &set) && !acctcreate(set, 4096, 8192, &acct));
  for (i = 0; i < 5; i++) {
    REQUIRE(!acctreserve(acct, 4096 - i));
    failed.generation = failed.index = failed.reserved = 77;
    REQUIRE(kstackcreate(acct, &failed) == AC_LIMIT);
    REQUIRE(failed.generation == 77 && failed.index == 77 && failed.reserved == 77);
    REQUIRE(!acctstat(acct, &account) && account.used == 0 && account.reserved == 4096 - i);
    pgstat(&current);
    REQUIRE(current.allocated == before.allocated && kernel->pages == pages);
    REQUIRE(!acctrelease(acct, 0, 4096 - i));
  }
  REQUIRE(!spacecreate(acct, &space));
  for (i = 0; i < KSTACKS; i++) {
    REQUIRE(!kstackcreate(acct, &pool[i]) && !kstacktop(pool[i], &top));
    REQUIRE(!kstackstat(pool[i], &record) && record.refs == 1 && record.table);
    REQUIRE(!spacelookup(space, top - 5 * PAGE_SIZE, &entry));
    REQUIRE(!spacelookup(space, top, &entry));
    for (j = 0; j < KSTACK_PAGES; j++)
      REQUIRE(spacelookup(space, top - (KSTACK_PAGES - j) * PAGE_SIZE, &entry) &&
              entry.pa == record.pa[j] && entry.prot == (VM_READ | VM_WRITE) &&
              entry.attr == VM_NORMAL);
  }
  REQUIRE(!acctstat(acct, &account) && account.used == 5 * KSTACKS + 1);
  REQUIRE(kstackcreate(acct, &failed) == KS_FULL && kstackinit(kernel) == KS_BUSY);
  for (i = 0; i < KSTACKS; i++)
    REQUIRE(!kstackfree(pool[i]));
  REQUIRE(!spacelookup(space, KSTACK_BASE + PAGE_SIZE, &entry));
  REQUIRE(!kstackcreate(acct, &running) && running.generation > pool[0].generation);
  REQUIRE(kstackfree(pool[0]) == KS_BAD);
  REQUIRE(!kstacktop(running, &top) && !kstackhold(running));
  hal_ctx_init(&worker, top, run);
  REQUIRE(arch_switchprobe(&caller, &worker) && resumed == 1);
  REQUIRE(kstackfree(running) == KS_BUSY);
  REQUIRE(arch_switchprobe(&caller, &worker) && resumed == 2);
  REQUIRE(!kstackused(running, &used) && used && used <= KSTACK_PAGES * PAGE_SIZE);
  for (i = 0; i < sizeof(worker); i++)
    ((u8 *) &worker)[i] = 0;
  REQUIRE(!acctclose(acct) && !acctunbind(acct));
  REQUIRE(!kstackdrop(running) && !kstackfree(running) && !spacedestroy(space));
  REQUIRE(acctstat(acct, &account) == AC_BAD && !setclose(set));
  pgstat(&after);
  REQUIRE(before.allocated == after.allocated && before.free == after.free &&
          kernel->pages == pages);
  REQUIRE(!acctcheck() && !pgcheck());
  putstr("STACKCHECK created=");
  puthex(KSTACKS);
  putstr(" resumed=");
  puthex(resumed);
  putstr(" depth=");
  puthex(used);
  putstr(" charged=");
  puthex(0);
  putstr(" allocated=");
  puthex(after.allocated);
  putstr("\n");
}
