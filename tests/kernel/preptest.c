#include "ktest.h"
#include <proc.h>
#include <prep.h>
#include <abi/boot.h>
#include <hal/context.h>
#include <diag.h>
#include <vm.h>
extern const u8 proc_begin[], proc_end[];
extern void     arch_proccheck(Hctx *context);
static u32      runs;
#define REQUIRE(c)                                                                                 \
  do {                                                                                             \
    if (!(c)) {                                                                                    \
      putstr("PREPFAIL line=");                                                                    \
      puthex(__LINE__);                                                                            \
      putstr("\n");                                                                                \
      panic("preparation loading");                                                                \
    }                                                                                              \
  } while (0)
static void
clean(Handle pid)
{
  int error;
  u32 i;
  for (i = 0; i < 32; i++) {
    error = procclean(pid);
    REQUIRE(error >= 0);
    if (!error)
      return;
  }
  panic("preparation process cleanup bound");
}
static void
sweep(Prepid id)
{
  Prepinfo p;
  u32      i;
  int      error;
  for (i = 0; i < 4; i++) {
    if (prepstat(id, &p) == -ERR_STALE)
      return;
    error = prepclean(id, 32);
    REQUIRE(error >= 0);
    if (!error)
      return;
  }
  panic("preparation source cleanup bound");
}
static Handle
resource(Rtable *t, u32 subtype)
{
  u32 i;
  for (i = 0; i < t->count; i++)
    if (t->slots[i].subtype == subtype)
      return handlepack(HANDLE_RESOURCE, t->slots[i].generation, i);
  panic("preparation source resource");
}
static Handle
allocate(Rtable *t, Handle construct, u64 bytes, u64 kind)
{
  Allocarg a;
  Handle   h;
  a.bytes = bytes;
  a.kind = kind;
  a.align = a.minaddr = a.maxaddr = a.reserved = 0;
  REQUIRE(!localalloc(t, construct, &a, &h));
  return h;
}
NORETURN void
preptesttrap(Hctx *context)
{
  Htrap trap;
  hal_trap_decode(context, &trap);
  REQUIRE(trap.fromuser && trap.kind == HT_SYSCALL && trap.syscall_nr == 1);
  arch_proccheck(context);
  runs++;
  procexit(LIFE_EXIT, 42, NULL);
}
void
preptest(void)
{
  Setid      set;
  Set        group;
  Rtable    *source, *target;
  Procinfo   parent, child, aborted, fresh;
  Prepinfo   scope;
  Prepid     id;
  Life       life;
  Liferesult result;
  Acct       account, payer;
  Acctid     located;
  Handle parentpid, pid, construct, sourcecap, code, stack, boot, permanent, oldcap, other, ignored;
  Allocarg args;
  Config   config;
  Boot     header;
  Pgstat   before, after;
  Hpte     alias;
  u64      oldpa;
  u32      i;
  pgstat(&before);
  REQUIRE(!setcreate(4096, 8192, 16, &set));
  REQUIRE(!procnew(set, 1, 256, 512, LOCALS, &parentpid) && !procinfo(parentpid, &parent));
  source = proctable(parentpid);
  REQUIRE(source != NULL);
  sourcecap = resource(source, LOCAL_SPACE);
  REQUIRE(!procnew(set, parentpid, 6, 6, LOCALS, &pid) &&
          !procprepare(source, pid, 31, &construct));
  REQUIRE(!localtarget(source, construct, PREP_ALLOC, &id, &ignored));
  args.bytes = PAGE_SIZE;
  args.kind = ALLOC_DATA;
  args.align = args.minaddr = args.maxaddr = args.reserved = 0;
  REQUIRE(localalloc(source, construct, &args, &ignored) == -ERR_LIMIT && !ignored);
  REQUIRE(!localdestroy(source, construct));
  clean(pid);
  sweep(id);
  REQUIRE(prepstat(id, &scope) == -ERR_STALE);
  REQUIRE(!procnew(set, parentpid, 256, 512, LOCALS, &pid) &&
          !procprepare(source, pid, 31, &construct));
  REQUIRE(!procinfo(pid, &child));
  target = proctable(pid);
  REQUIRE(target != NULL);
  REQUIRE(!localtarget(source, construct, PREP_CONFIG, &id, &ignored));
  code = allocate(source, construct, PAGE_SIZE, ALLOC_CODE);
  REQUIRE(!acctstat(parent.acct, &payer) && payer.used == 6);
  REQUIRE(!acctstat(child.acct, &account) && account.used == 7);
  REQUIRE(!localwrite(source, code, 0, proc_begin, (u64) (proc_end - proc_begin)));
  REQUIRE(!localmap(source, sourcecap, code, 0, 0x100000, PAGE_SIZE, VM_READ | VM_WRITE));
  REQUIRE(localseal(source, code) == -ERR_BUSY);
  REQUIRE(!localunmap(source, sourcecap, 0x100000) && !localseal(source, code));
  REQUIRE(!localmap(source, sourcecap, code, 0, 0x100000, PAGE_SIZE, VM_READ));
  REQUIRE(!localmap(source, construct, code, 0, 0x10000, PAGE_SIZE, VM_READ | VM_EXEC));
  REQUIRE(
      !localcopy(source, code, target, VM_READ | VM_EXEC, 0, PAGE_SIZE, LOCAL_FORK, &permanent));
  stack = allocate(source, construct, 65536, ALLOC_DATA);
  boot = allocate(source, construct, PAGE_SIZE, ALLOC_DATA);
  for (i = 0; i < sizeof(header); i++)
    ((u8 *) &header)[i] = 0;
  header.magic = BOOT_MAGIC;
  header.format = 1;
  header.bytes = sizeof(header);
  header.user_lo = 0x10000;
  header.user_hi = 1UL << 38;
  REQUIRE(!localwrite(source, boot, 0, &header, sizeof(header)));
  REQUIRE(!localmap(source, construct, stack, 0, 0x3ffffe0000UL, 65536, VM_READ | VM_WRITE));
  REQUIRE(!localmap(source, construct, boot, 0, 0x3fff000000UL, PAGE_SIZE, VM_READ));
  config.entry = 0x10000;
  config.stack = 0x3fffff0000UL;
  config.boot = 0x3fff000000UL;
  config.bootsz = sizeof(header);
  config.sigmask = config.sigdefault = 0;
  REQUIRE(!localconfigure(source, construct, &config, 0));
  REQUIRE(localstart(source, construct) == -ERR_BUSY);
  REQUIRE(!localunmap(source, sourcecap, 0x100000));
  REQUIRE(!localmap(source, sourcecap, stack, 0, 0x200000, 65536, VM_READ | VM_WRITE));
  REQUIRE(localstart(source, construct) == -ERR_BUSY);
  REQUIRE(!localunmap(source, sourcecap, 0x200000));
  REQUIRE(!localstart(source, construct));
  REQUIRE(localdestroy(source, construct) == -ERR_BAD_HANDLE);
  REQUIRE(localwrite(source, boot, 0, &header, sizeof(header)) == -ERR_REVOKED);
  REQUIRE(localseal(source, code) == -ERR_REVOKED);
  sweep(id);
  REQUIRE(prepstat(id, &scope) == -ERR_STALE);
  REQUIRE(!procrunone() && runs == 1);
  clean(pid);
  REQUIRE(!lifecollect(child.life, parentpid, &result) && result.pid == pid && result.code == 42);
  /* Cancellation revokes authority but a surviving source alias still owns its page. */
  REQUIRE(!procnew(set, parentpid, 256, 512, LOCALS, &pid) &&
          !procprepare(source, pid, 31, &construct));
  oldcap = construct;
  REQUIRE(!procinfo(pid, &aborted) && !localtarget(source, construct, PREP_ALLOC, &id, &ignored));
  code = allocate(source, construct, PAGE_SIZE, ALLOC_DATA);
  REQUIRE(!localmap(source, sourcecap, code, 0, 0x100000, PAGE_SIZE, VM_READ | VM_WRITE));
  REQUIRE(spacelookup(parent.space, 0x100000, &alias));
  oldpa = alias.pa;
  REQUIRE(!localdestroy(source, construct));
  clean(pid);
  sweep(id);
  REQUIRE(!acctstat(aborted.acct, &account) && account.state == AC_CLOSING && account.used == 1);
  REQUIRE(!lifestat(aborted.life, &life) && life.cleanup == 1 && life.run == PG_NONE);
  REQUIRE(!lifeacct(aborted.life, &located) && located.generation == aborted.acct.generation &&
          located.index == aborted.acct.index);
  REQUIRE(!procnew(set, parentpid, 256, 512, LOCALS, &other) && !procinfo(other, &fresh));
  REQUIRE(handleindex(other) == handleindex(pid) && other != pid &&
          fresh.acct.index != aborted.acct.index);
  REQUIRE(!lifeacct(aborted.life, &located) && located.generation == aborted.acct.generation &&
          located.index == aborted.acct.index && !lifeacct(fresh.life, &located) &&
          located.generation == fresh.acct.generation && located.index == fresh.acct.index);
  REQUIRE(localdestroy(source, oldcap) == -ERR_BAD_HANDLE);
  REQUIRE(spacelookup(parent.space, 0x100000, &alias) && alias.pa == oldpa);
  REQUIRE(!localunmap(source, sourcecap, 0x100000));
  REQUIRE(acctstat(aborted.acct, &account) == AC_BAD &&
          lifestat(aborted.life, &life) == -ERR_STALE);
  REQUIRE(!procdestroy(other));
  clean(other);
  /* Source death cancels its unpublished child; CPU cleanup may proceed independently. */
  REQUIRE(!procnew(set, parentpid, 256, 512, LOCALS, &pid) &&
          !procprepare(source, pid, 31, &construct));
  code = allocate(source, construct, PAGE_SIZE, ALLOC_DATA);
  REQUIRE(!procdestroy(parentpid));
  clean(parentpid);
  for (i = 0; i < 64; i++) {
    REQUIRE(procwork(32) >= 0);
    REQUIRE(prepwork(32) >= 0);
  }
  REQUIRE(procinfo(pid, &fresh) == -ERR_STALE);
  REQUIRE(!setstat(set, &group) && !group.charged && !group.instances && !setclose(set));
  pgstat(&after);
  REQUIRE(before.allocated == after.allocated && before.free == after.free && !acctcheck() &&
          !pgcheck());
  putstr("PREPCHECK runs=");
  puthex(runs);
  putstr(" exit=");
  puthex(42);
  putstr(" retained=");
  puthex(1);
  putstr(" charged=");
  puthex(group.charged);
  putstr(" allocated=");
  puthex(after.allocated);
  putstr("\n");
}
