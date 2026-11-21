#include "ktest.h"
#include <proc.h>
#include <prep.h>
#include <abi/boot.h>
#include <diag.h>
#include <cow.h>
#include <signal.h>
#include <vm.h>
static Handle   parentpid, peerpid, killer;
static Rtable  *parenttable, *peertable, *childtable;
static Procinfo parentinfo, childinfo;
static Prepid   preparation;
static Spaceid  aliasroot;
static u32      armed, mode, reached, peerprogress, resumed, churn;
static u32      selfstopped;
static u32      aliasdenied;
NORETURN void   __real_hal_user_enter(Hctx *context);
#define REQUIRE(c)                                                                                 \
  do {                                                                                             \
    if (!(c)) {                                                                                    \
      putstr("FORKCONTFAIL line=");                                                                \
      puthex(__LINE__);                                                                            \
      putstr("\n");                                                                                \
      panic("fork kernel continuation");                                                           \
    }                                                                                              \
  } while (0)
static void
bootstrap(Handle pid)
{
  Rtable  *table = proctable(pid);
  Procinfo process;
  Handle   space = 0, alloc = 0, code, stack, boot;
  Allocarg args;
  Config   config;
  Boot     header;
  u32      i;
  REQUIRE(table && !procinfo(pid, &process));
  for (i = 0; i < table->count; i++) {
    if (table->slots[i].subtype == LOCAL_SPACE)
      space = handlepack(HANDLE_RESOURCE, table->slots[i].generation, i);
    if (table->slots[i].subtype == LOCAL_ALLOC)
      alloc = handlepack(HANDLE_RESOURCE, table->slots[i].generation, i);
  }
  args.kind = ALLOC_CODE;
  args.bytes = PAGE_SIZE;
  args.align = args.minaddr = args.maxaddr = args.reserved = 0;
  REQUIRE(!localalloc(table, alloc, &args, &code) && !localseal(table, code) &&
          !localmap(table, space, code, 0, 0x10000, PAGE_SIZE, VM_READ | VM_EXEC));
  args.kind = ALLOC_DATA;
  args.bytes = 16 * PAGE_SIZE;
  REQUIRE(!localalloc(table, alloc, &args, &stack) &&
          !localmap(table, space, stack, 0, 0x70000, 16 * PAGE_SIZE, VM_READ | VM_WRITE));
  args.bytes = PAGE_SIZE;
  REQUIRE(!localalloc(table, alloc, &args, &boot));
  for (i = 0; i < sizeof(header); i++)
    ((u8 *) &header)[i] = 0;
  header.magic = BOOT_MAGIC;
  header.format = BOOT_FORMAT;
  header.bytes = sizeof(header);
  header.user_lo = BOOT_USER_LO;
  header.user_hi = BOOT_USER_HI;
  REQUIRE(!localwrite(table, boot, 0, &header, sizeof(header)) &&
          !localmap(table, space, boot, 0, 0x50000, PAGE_SIZE, VM_READ));
  config.entry = 0x10000;
  config.stack = 0x80000;
  config.boot = 0x50000;
  config.bootsz = sizeof(header);
  config.sigmask = config.sigdefault = 0;
  REQUIRE(!procconfigure(pid, &config, 0));
}
static NORETURN void
builder(void)
{
  Hpte pte;
  u32  i;
  int  error;
  REQUIRE(procforkleave() == -ERR_STATE);
  procpausevm();
  REQUIRE(
      !prepforkcreate(parenttable, childtable, childinfo.life, childinfo.identity, &preparation) &&
      !spacesnapshotopen(parentinfo.space, childinfo.space, preparation) &&
      !procforkenter(preparation) && procforkenter(preparation) == -ERR_STATE);
  for (i = 0; i < 100; i++) {
    error = spacesnapshotbuild(parentinfo.space, childinfo.space);
    REQUIRE(error == 0 || error == 1);
    REQUIRE(spacelookup(parentinfo.space, 0x70000, &pte));
    if (!(pte.prot & VM_WRITE))
      break;
  }
  REQUIRE(i < 100 && spacefrozen(parentinfo.space) == 1 && spacefrozen(childinfo.space) == 1);
  reached = 1;
  if (mode == 2)
    procexit(LIFE_SIGNAL, 9, NULL);
  if (!mode) {
    REQUIRE(!procstop(parentpid, SIGNAL_STOP));
    selfstopped++;
  }
  procforkyield();
  resumed++;
  REQUIRE(!mode && peerprogress == 1 && reached == 1 && spacefrozen(parentinfo.space) == 1 &&
          spacedeactivate() == SP_BAD && spaceactivate(parentinfo.space) == SP_BUSY);
  REQUIRE(!prepcancel(preparation) && !spacesnapshotend(parentinfo.space, childinfo.space, 0) &&
          !procforkleave() && prepforkfinish(preparation) == -ERR_REVOKED);
  procresumevm();
  REQUIRE(procforkleave() == -ERR_STATE);
  procexit(LIFE_EXIT, 42, NULL);
}
NORETURN void
__wrap_hal_user_enter(Hctx *context)
{
  Rtable *table = proccurrent(context);
  faultframeenter(context);
  runtimeframeenter(context);
  if (armed && table) {
    if (table->owner == parentpid)
      builder();
    if (table->owner == peerpid) {
      Allocarg args;
      Handle   allocator = 0, space = 0, memory;
      u32      i;
      u64      value = 0x5151;
      REQUIRE(reached == 1 && !resumed && spacefrozen(parentinfo.space) == 1);
      for (i = 0; i < table->count; i++) {
        if (table->slots[i].subtype == LOCAL_ALLOC)
          allocator = handlepack(HANDLE_RESOURCE, table->slots[i].generation, i);
        if (table->slots[i].subtype == LOCAL_SPACE)
          space = handlepack(HANDLE_RESOURCE, table->slots[i].generation, i);
      }
      REQUIRE(allocator && space);
      args.bytes = PAGE_SIZE;
      args.kind = ALLOC_DATA;
      args.align = args.minaddr = args.maxaddr = args.reserved = 0;
      procpausevm();
      if (mode < 2) {
        Memid  original;
        Mem    beforemem, aftermem;
        Space  beforemap, aftermap;
        Pgstat beforepages, afterpages;
        u32    found = 0, byte;
        for (i = 0; i < parenttable->count; i++)
          if (parenttable->slots[i].subtype == LOCAL_MEM &&
              parenttable->slots[i].length == 16 * PAGE_SIZE) {
            original.generation = parenttable->slots[i].objectgen;
            original.index = parenttable->slots[i].object;
            original.reserved = 0;
            found++;
          }
        REQUIRE(found == 1 && !spacefrozen(aliasroot) && !memstat(original, &beforemem) &&
                !spacestat(aliasroot, &beforemap));
        pgstat(&beforepages);
        REQUIRE(spacemap(aliasroot, original, 0, 0x100000, PAGE_SIZE, VM_READ) == SP_BUSY &&
                spacemap(aliasroot, original, 0, 0x100000, PAGE_SIZE, VM_READ | VM_WRITE) ==
                    SP_BUSY &&
                spacecowmap(aliasroot, original, 0, 0x100000, PAGE_SIZE) == SP_BUSY);
        aliasdenied += 3;
        pgstat(&afterpages);
        REQUIRE(!memstat(original, &aftermem) && !spacestat(aliasroot, &aftermap) &&
                beforemap.pages == aftermap.pages && beforemap.mapcount == aftermap.mapcount &&
                beforepages.allocated == afterpages.allocated &&
                beforepages.free == afterpages.free);
        for (byte = 0; byte < sizeof(beforemem); byte++)
          REQUIRE(((const u8 *) &beforemem)[byte] == ((const u8 *) &aftermem)[byte]);
      }
      for (i = 0; i < 5; i++) {
        REQUIRE(!localalloc(table, allocator, &args, &memory) &&
                !localmap(table, space, memory, 0, 0x100000, PAGE_SIZE, VM_READ | VM_WRITE) &&
                !localwrite(table, memory, 0, &value, sizeof(value)) &&
                !localunmap(table, space, 0x100000) && !localfree(table, memory));
        churn++;
      }
      procresumevm();
      peerprogress++;
      if (mode == 1)
        REQUIRE(!procterminate(peertable, killer));
      procexit(LIFE_EXIT, 42, NULL);
    }
  }
  __real_hal_user_enter(context);
}
void
forkcontinuetest(void)
{
  Setid      set;
  Set        group;
  Handle     child;
  Procinfo   peer, stale;
  Prepinfo   prep;
  Life       life;
  Liferesult result;
  Cowpool    pool;
  Pgstat     before, after;
  u32        i, j, work, continued = 0, killed = 0, selfkilled = 0, stopped = 0;
  int        error;
  pgstat(&before);
  churn = selfstopped = aliasdenied = 0;
  for (mode = 0; mode < 3; mode++) {
    reached = peerprogress = resumed = 0;
    preparation.generation = 0;
    REQUIRE(!setcreate(1024, 1024, 8, &set) && !procnew(set, 1, 128, 128, 64, &parentpid) &&
            !procinfo(parentpid, &parentinfo) && !procnew(set, 1, 128, 128, 16, &peerpid) &&
            !procinfo(peerpid, &peer) && !procnew(set, parentpid, 128, 128, 64, &child) &&
            !procinfo(child, &childinfo) && !spacecreate(parentinfo.acct, &aliasroot));
    parenttable = proctable(parentpid);
    peertable = proctable(peerpid);
    childtable = proctable(child);
    REQUIRE(parenttable && peertable && childtable);
    bootstrap(parentpid);
    bootstrap(peerpid);
    REQUIRE(!localcontrol(peertable, parentinfo.life, CONTROL_TERMINATE, &killer));
    for (i = 0; i < childtable->count; i++)
      if (childtable->slots[i].subtype)
        REQUIRE(!localrevoke(childtable,
                             handlepack(HANDLE_RESOURCE, childtable->slots[i].generation, i)));
    REQUIRE(!procpublish(parentpid) && !procpublish(peerpid));
    armed = 1;
    REQUIRE(!procrunone() && reached == 1 && !resumed && !peerprogress);
    if (mode < 2) {
      REQUIRE(!procstop(parentpid, SIGNAL_STOP) && !procinfo(parentpid, &stale) &&
              stale.state == PROC_STOPPED && !stale.phase && !prepstat(preparation, &prep) &&
              prep.cloning && spacefrozen(parentinfo.space) == 1);
      stopped++;
    }
    REQUIRE(!procrunone() && peerprogress == 1 && !resumed);
    if (!mode) {
      REQUIRE(procrunone() == -ERR_WOULD_BLOCK && !proccontinue(parentpid));
      REQUIRE(!procrunone() && resumed == 1);
      continued++;
    } else {
      REQUIRE(procrunone() == -ERR_WOULD_BLOCK);
      if (mode == 1)
        killed++;
      else
        selfkilled++;
    }
    armed = 0;
    REQUIRE(!lifestat(parentinfo.life, &life) && life.state == LIFE_ENDED &&
            !prepstat(preparation, &prep) && prep.state == PREP_ABORTED);
    work = 0;
    for (i = 0; i < 1000 && prep.cloning; i++) {
      error = prepwork(32);
      REQUIRE(error >= 0);
      work += (u32) error;
      REQUIRE(procwork(32) >= 0 && !prepstat(preparation, &prep));
    }
    REQUIRE(i < 1000 && !prep.cloning && !prepdrop(preparation));
    for (j = 0; j < 3; j++) {
      Handle pid = j == 0 ? parentpid : (j == 1 ? peerpid : child);
      for (i = 0; i < 64; i++) {
        error = procclean(pid);
        if (error == -ERR_STALE)
          break;
        REQUIRE(error >= 0);
        if (!error)
          break;
      }
      REQUIRE(i < 64 && procinfo(pid, &stale) == -ERR_STALE);
    }
    REQUIRE(!lifecollect(parentinfo.life, 1, &result) &&
            result.reason == (mode ? LIFE_SIGNAL : LIFE_EXIT) && result.code == (mode ? 9U : 42U) &&
            !lifecollect(peer.life, 1, &result) && result.reason == LIFE_EXIT && result.code == 42);
    REQUIRE(!spacedestroy(aliasroot));
    cowpool(&pool);
    REQUIRE(!pool.groups && !pool.branches && !pool.prepaid && !pgreserved() && !cowcheck() &&
            !acctcheck() && !pgcheck() && !lifecheck() && !groupcheck() && !sigcheck() &&
            !setstat(set, &group) && !group.charged && !group.instances && !setclose(set));
    if (mode)
      REQUIRE(work > 2);
  }
  pgstat(&after);
  REQUIRE(before.free == after.free && before.allocated == after.allocated);
  putstr("FORKCONTCHECK cases=");
  puthex(3);
  putstr(" resumed=");
  puthex(continued);
  putstr(" killed=");
  puthex(killed);
  putstr(" self_killed=");
  puthex(selfkilled);
  putstr(" stopped=");
  puthex(stopped);
  putstr(" self_stop=");
  puthex(selfstopped);
  putstr(" peer_progress=");
  puthex(3);
  putstr(" vm_churn=");
  puthex(churn);
  putstr(" alias_denied=");
  puthex(aliasdenied);
  putstr(" charged=");
  puthex(0);
  putstr(" allocated=");
  puthex(after.allocated);
  putstr("\n");
}
