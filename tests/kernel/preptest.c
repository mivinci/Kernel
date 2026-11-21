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
static void
execowtest(void)
{
  static Memretire retire;
  Setid            set;
  Acctid           payer, peer;
  Memid            old, child;
  Set              group;
  Acct             account;
  u64              before, after;
  REQUIRE(!setcreate(2, 2, 2, &set) && !acctcreate(set, 1, 1, &payer) &&
          !acctcreate(set, 1, 1, &peer) && !memcreate(payer, PAGE_SIZE, &old) &&
          !memclone(old, peer, &child) && !mempage(old, 0, &before));
  REQUIRE(!memretireadd(&retire, old) && memretirepages(&retire, payer) == 1 &&
          !setstat(set, &group) && group.charged == 2);
  memretireclear(&retire);
  REQUIRE(!memfree(old) && !mempage(child, 0, &after) && before == after &&
          !acctstat(payer, &account) && !account.used && !account.reserved &&
          !acctstat(peer, &account) && account.used == 1 && !account.reserved &&
          !setstat(set, &group) && group.charged == 1 && !memfree(child) && !acctclose(payer) &&
          !acctunbind(payer) && !acctclose(peer) && !acctunbind(peer) && !setclose(set));
}
static void
execpreptest(void)
{
  static Rtable source, target;
  static Local  oldslots[16], newslots[16];
  static u64    generations[16];
  Setid         set;
  Set           group;
  Acctid        acct;
  Spaceid       oldspace, newspace;
  Lifeid        life;
  Life          instance;
  Liferesult    result;
  Imageid       running, future;
  Image         image;
  Prepid        prep, rejected;
  Prepinfo      info;
  Rtable       *queried;
  Memid         original;
  Pgstat        before, after;
  Hpte          alias;
  Handle        pid = handlepack(HANDLE_PROCESS, 1, PROCS - 1);
  Handle        oldcap, futurecap, previous = 0, view, data, ignored, futurealloc;
  Handle        owner;
  u64           ticket, duplicate, marker = 0x31415926, value;
  u32           mode, i;
  u64           va = 0x10000;
  pgstat(&before);
  REQUIRE(!setcreate(4, 10, 1, &set) && !acctcreate(set, 4, 10, &acct) &&
          !lifecreate(set, pid, 1, &life) && !lifebindacct(life, acct) &&
          !imagecreate(life, 0, &running) && !lifepublish(life));
  REQUIRE(!spacecreate(acct, &oldspace) && !localinit(&source, oldslots, 16, acct, oldspace));
  source.owner = pid;
  source.image = 1;
  source.generations = generations;
  generations[1] = HANDLE_GENERATION_MAX - 1;
  REQUIRE(!localspace(&source, 3, &oldcap) && !localreserve(&source, running, 1) &&
          source.slots[1].subtype == LOCAL_FLOOR && !imageactivate(running) &&
          !memcreate(acct, PAGE_SIZE, &original) &&
          !spacemap(oldspace, original, 0, va, PAGE_SIZE, VM_READ | VM_WRITE) &&
          !spacemap(oldspace, original, 0, va + 2 * PAGE_SIZE, PAGE_SIZE, VM_READ) &&
          !spacewrite(oldspace, va, &marker, sizeof(marker)));
  for (mode = 0; mode < 3; mode++) {
    REQUIRE(!acctexecbegin(acct, &ticket) && !spaceexeccreate(acct, ticket, &newspace) &&
            !localinit(&target, newslots, 16, acct, newspace) &&
            !imagecreate(life, source.image, &future) && !imagestat(future, &image));
    target.owner = pid;
    target.image = image.number;
    target.generations = generations;
    source.parallel = &target;
    target.parallel = &source;
    REQUIRE(!localspace(&target, 3, &futurecap) && handleindex(futurecap) == handleindex(oldcap) &&
            handlegen(futurecap) > handlegen(previous) && futurecap != oldcap &&
            !localnative(&source, oldcap) && localnative(&source, futurecap) == -ERR_BAD_HANDLE &&
            localnative(&target, oldcap) == -ERR_BAD_HANDLE &&
            !localallocator(&target, 7, &futurealloc) && handleindex(futurealloc) == 2 &&
            generations[1] == HANDLE_GENERATION_MAX - 1 &&
            prepexecopen(&source, &source, life, future, ticket, &rejected) == -ERR_INVALID &&
            !prepexecopen(&source, &target, life, future, ticket, &prep) &&
            acctexecbegin(acct, &duplicate) == AC_BUSY &&
            prepexecopen(&source, &target, life, future, ticket, &rejected) == -ERR_BUSY);
    REQUIRE(!localconstruct(&source, prep, 31, &view) &&
            handleindex(view) == handleindex(futurealloc) &&
            handlegen(view) > handlegen(futurealloc) &&
            localnative(&source, futurealloc) == -ERR_BAD_HANDLE &&
            localnative(&target, view) == -ERR_BAD_HANDLE &&
            source.slots[handleindex(view)].subtype == LOCAL_EXEC_VIEW &&
            !localqueryprep(&source, view, &queried) && queried == &target &&
            localtarget(&source, view, PREP_PUBLISH, &rejected, &ignored) == -ERR_BAD_HANDLE);
    REQUIRE(prepexecticket(prep, &target, &duplicate) == -ERR_BAD_HANDLE);
    data = allocate(&source, view, PAGE_SIZE, ALLOC_DATA);
    REQUIRE(localmap(&source, oldcap, data, 0, 1UL << 21, PAGE_SIZE, VM_READ) == -ERR_LIMIT &&
            !localmap(&source, oldcap, data, 0, va + PAGE_SIZE, PAGE_SIZE, VM_READ | VM_WRITE) &&
            !spacewrite(oldspace, va + PAGE_SIZE, &marker, sizeof(marker)) &&
            !localmap(&source, view, data, 0, va, PAGE_SIZE, VM_READ) &&
            !spaceread(newspace, va, &value, sizeof(value)) && value == marker &&
            preppublishcheck(prep) == -ERR_BUSY && !prepstat(prep, &info) && info.temporary == 1 &&
            !setstat(set, &group) && group.charged == 8);
    REQUIRE(prepexecbudget(prep, &target) == -ERR_BAD_HANDLE &&
            prepexecbudget(prep, &source) == -ERR_BUSY);
    if (!mode) {
      REQUIRE(!localmem(&source, original, VM_READ, 0, PAGE_SIZE, 0, &owner) &&
              !memfree(original) && !localunmap(&source, oldcap, va + PAGE_SIZE) &&
              !memretain(original) && prepexecbudget(prep, &source) == -ERR_LIMIT &&
              !memrelease(original) && !spacehold(oldspace) &&
              prepexecbudget(prep, &source) == -ERR_BUSY && !spacedrop(oldspace) &&
              !prepexecbudget(prep, &source) && !setstat(set, &group) && group.charged == 8 &&
              !localfree(&source, owner) &&
              !localmap(&source, oldcap, data, 0, va + PAGE_SIZE, PAGE_SIZE, VM_READ | VM_WRITE));
    }
    if (mode == 0)
      REQUIRE(!prepcancel(prep));
    else if (mode == 1) {
      for (i = 0; i < PREPS / 8; i++)
        REQUIRE(prepexpire(info.deadline, 8) >= 0);
      REQUIRE(preppublishcheck(prep) == -ERR_TIMEDOUT &&
              prepexecbudget(prep, &source) == -ERR_TIMEDOUT);
    } else
      prepownerclose(&source);
    REQUIRE(!acctexecvalid(acct, ticket) &&
            localqueryprep(&source, view, &queried) == -ERR_BAD_HANDLE &&
            localwrite(&source, data, 0, &marker, sizeof(marker)) == -ERR_REVOKED &&
            !imagestat(future, &image) && image.state == IMAGE_ENDED &&
            image.reason == (mode == 1 ? IMAGE_TIMEOUT : IMAGE_ABORT) &&
            !imagestat(running, &image) && image.state == IMAGE_ACTIVE &&
            !lifestat(life, &instance) && instance.state == LIFE_LIVE);
    sweep(prep);
    previous = futurecap;
    source.parallel = target.parallel = NULL;
    REQUIRE(!localclose(&target) && !localdrain(&target, 32) && !spaceclear(newspace, 32) &&
            !spacedestroy(newspace) && !imagedetach(future) && !prepdrop(prep));
    REQUIRE(spacelookup(oldspace, va + PAGE_SIZE, &alias) && !setstat(set, &group) &&
            group.charged == 5 && !localunmap(&source, oldcap, va + PAGE_SIZE) &&
            prepstat(prep, &info) == -ERR_STALE && !setstat(set, &group) && group.charged == 4 &&
            !spaceread(oldspace, va, &value, sizeof(value)) && value == marker);
  }
  REQUIRE(!localclose(&source) && !localdrain(&source, 32) && !spaceunmap(oldspace, va) &&
          !spaceunmap(oldspace, va + 2 * PAGE_SIZE) && !spacedestroy(oldspace) &&
          !lifeend(life, LIFE_EXIT, 0, NULL) && !imageend(running, IMAGE_EXIT) &&
          !imagedetach(running) && !lifedetach(life) && !lifecollect(life, 1, &result) &&
          !acctclose(acct) && !acctunbind(acct) && !setclose(set));
  pgstat(&after);
  REQUIRE(before.allocated == after.allocated && before.free == after.free && !acctcheck() &&
          !pgcheck());
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
  execowtest();
  execpreptest();
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
  header.format = BOOT_FORMAT;
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
