#include "ktest.h"
#include <proc.h>
#include <rq.h>
#include <prep.h>
#include <signal.h>
#include <cow.h>
#include <abi/boot.h>
#include <diag.h>
static Local  slots[16];
static Rtable source;
void          arch_forktrap(Hctx *context);
#define REQUIRE(c)                                                                                 \
  do {                                                                                             \
    if (!(c)) {                                                                                    \
      putstr("FORKLIFEFAIL line=");                                                                \
      puthex(__LINE__);                                                                            \
      putstr("\n");                                                                                \
      panic("fork preparation lifecycle");                                                         \
    }                                                                                              \
  } while (0)
static void
clean(Handle pid)
{
  u32 i;
  int error;
  for (i = 0; i < 64; i++) {
    error = procclean(pid);
    REQUIRE(error >= 0);
    if (!error)
      return;
  }
  panic("fork cleanup bound");
}
static void
sweep(Prepid prep)
{
  Prepinfo state;
  u32      i;
  for (i = 0; i < 4; i++) {
    if (prepstat(prep, &state) == -ERR_STALE)
      return;
    REQUIRE(prepclean(prep, 32) >= 0);
  }
  REQUIRE(prepstat(prep, &state) == -ERR_STALE || !state.target);
}
static Handle
resource(Rtable *table, u32 subtype)
{
  u32 i;
  for (i = 0; i < table->count; i++)
    if (table->slots[i].subtype == subtype)
      return handlepack(HANDLE_RESOURCE, table->slots[i].generation, i);
  panic("fork death source resource");
}
static void
parentdeath(void)
{
  Setid    set;
  Set      group;
  Procinfo parent, child, stale;
  Handle   pid, target, construct, space, allocator, code, stack, boot, creator, killer, ignored;
  Rtable  *table;
  Allocarg request;
  Createpolicy policy;
  Config       config;
  Boot         header;
  Prepid       id;
  Prepinfo     prep;
  Life         life;
  Liferesult   result;
  Acct         account;
  Hctx        *user;
  u64          top;
  u32          i;
  REQUIRE(!setcreate(512, 512, 8, &set) && !procnew(set, 1, 128, 128, 16, &pid) &&
          !procinfo(pid, &parent));
  table = proctable(pid);
  REQUIRE(table != NULL);
  space = resource(table, LOCAL_SPACE);
  allocator = resource(table, LOCAL_ALLOC);
  request.bytes = PAGE_SIZE;
  request.kind = ALLOC_CODE;
  request.align = request.minaddr = request.maxaddr = request.reserved = 0;
  REQUIRE(!localalloc(table, allocator, &request, &code) && !localseal(table, code) &&
          !localmap(table, space, code, 0, 0x10000, PAGE_SIZE, VM_READ | VM_EXEC));
  request.bytes = 65536;
  request.kind = ALLOC_DATA;
  REQUIRE(!localalloc(table, allocator, &request, &stack) &&
          !localmap(table, space, stack, 0, 0x70000, 65536, VM_READ | VM_WRITE));
  request.bytes = PAGE_SIZE;
  REQUIRE(!localalloc(table, allocator, &request, &boot));
  for (i = 0; i < sizeof(header); i++)
    ((u8 *) &header)[i] = 0;
  header.magic = BOOT_MAGIC;
  header.format = BOOT_FORMAT;
  header.bytes = sizeof(header);
  header.user_lo = BOOT_USER_LO;
  header.user_hi = BOOT_USER_HI;
  REQUIRE(!localwrite(table, boot, 0, &header, sizeof(header)) &&
          !localmap(table, space, boot, 0, 0x50000, PAGE_SIZE, VM_READ));
  policy.set = set;
  policy.memlimit = policy.exec_limit = 128;
  policy.resources = 16;
  policy.pending = PENDING_CALLS;
  policy.rights = CREATE_RIGHTS | LOCAL_GRANT;
  REQUIRE(!localcreate(table, &policy, &creator) &&
          !localcontrol(table, parent.life, CONTROL_TERMINATE, &killer));
  config.entry = 0x10000;
  config.stack = 0x80000;
  config.boot = 0x50000;
  config.bootsz = sizeof(header);
  config.sigmask = config.sigdefault = 0;
  REQUIRE(!procconfigure(pid, &config, 0) && !procpublish(pid) && !kstacktop(parent.stack, &top));
  user = (Hctx *) (top - sizeof(Hctx));
  arch_forktrap(user);
  REQUIRE(!procforkbuild(set, table, user, PENDING_CALLS, 1, &construct, &target) &&
          !procinfo(target, &child) && !localtarget(table, construct, PREP_CONFIG, &id, &ignored) &&
          !prephold(id));
  REQUIRE(!procterminate(table, killer) && table->state == LOCAL_OPEN && !prepstat(id, &prep) &&
          prep.state == PREP_ABORTED && !prep.target && !lifestat(child.life, &life) &&
          life.state == LIFE_ABORTED && !life.waitref);
  /* Parent root/account can retire first; child's canonical/COW backing keeps
     the old payer charged until its last independent view is destroyed. */
  clean(pid);
  REQUIRE(!acctstat(parent.acct, &account) && account.state == AC_CLOSING && account.used);
  clean(target);
  REQUIRE(procinfo(target, &stale) == -ERR_STALE && !prepdrop(id) &&
          prepstat(id, &prep) == -ERR_STALE && !lifecollect(parent.life, 1, &result) &&
          result.reason == LIFE_SIGNAL && result.code == 9 && !setstat(set, &group) &&
          !group.charged && !group.instances && !setclose(set) && !acctcheck() && !pgcheck() &&
          !lifecheck() && !groupcheck() && !sigcheck());
}

void
forklifetest(void)
{
  Setid        set;
  Set          group;
  Acctid       acct;
  Acct         account;
  Spaceid      space;
  Space        tree;
  Lifeid       parentlife;
  Life         parent, childlife;
  Imageid      image;
  Image        identity;
  Handle       parentpid, selfspace, selfalloc, codecap, stackcap, createcap;
  Handle       views[3], children[3], failedview, failedpid;
  Procinfo     child[3], stale;
  Prepid       preparations[3];
  Prepinfo     prep;
  Memid        code, stack;
  Createpolicy policy, inherited;
  Cred         cred;
  Hinit        initial;
  Hctx         context;
  Hctx        *childuser;
  Sigact       action;
  Pgstat       before, staged, after;
  Cowpool      pool;
  Hpte         original, leaf;
  u64          top;
  u32          mode, i, j, count, accepted = 0, cancelled = 0, failed = 0;
  int          error;
  pgstat(&before);
  for (mode = 0; mode < 5; mode++) {
    REQUIRE(!setcreate(512, 512, 8, &set) && !acctcreate(set, 64, 64, &acct) &&
            !spacecreate(acct, &space));
    parentpid = handlepack(HANDLE_PROCESS, 5000 + mode, 50);
    REQUIRE(!lifecreate(set, parentpid, 1, &parentlife) && !lifebindacct(parentlife, acct) &&
            !imagecreate(parentlife, 0, &image) && !sigreserve(image));
    cred.ruid = 1000;
    cred.euid = 1001;
    cred.rgid = 2000;
    cred.egid = 2001;
    REQUIRE(!lifecredentials(parentlife, &cred) && !localinit(&source, slots, 16, acct, space));
    source.owner = parentpid;
    source.image = 1;
    REQUIRE(!localspace(&source, 3, &selfspace) && !localallocator(&source, 7, &selfalloc) &&
            !memcode(acct, PAGE_SIZE, &code) && !memseal(code) &&
            !localmem(&source, code, 5, 0, PAGE_SIZE, LOCAL_FORK, &codecap) &&
            !spacemap(space, code, 0, 0x10000, PAGE_SIZE, VM_READ | VM_EXEC) &&
            !memcreate(acct, 5 * PAGE_SIZE, &stack) &&
            !localmem(&source, stack, 3, 0, 5 * PAGE_SIZE, LOCAL_FORK, &stackcap) &&
            !spacemap(space, stack, 0, 0x7c000, 5 * PAGE_SIZE, VM_READ | VM_WRITE));
    policy.set = set;
    policy.memlimit = policy.exec_limit = 64;
    policy.resources = 16;
    policy.pending = 1;
    policy.rights = CREATE_RIGHTS | LOCAL_GRANT;
    REQUIRE(!localcreate(&source, &policy, &createcap) && !lifebootgroup(parentlife) &&
            !lifepublish(parentlife) && !imageactivate(image) && !lifestat(parentlife, &parent));
    initial.entry = 0x10000;
    initial.sp = 0x80040;
    initial.arg = 0x1234;
    hal_user_init(&context, &initial);
    arch_forktrap(&context);
    if (mode == 3) {
      action.kind = SIGNAL_HANDLER;
      action.handler = 0x40000;
      action.trampoline = 0x10040;
      action.mask = action.flags = action.reserved = 0;
      REQUIRE(!siginstall(image, SIGNAL_USR1, &action) && spacelookup(space, 0x7c000, &original));
      pgstat(&staged);
      failedview = failedpid = 77;
      REQUIRE(procforkbuild(set, &source, &context, 1, 1, &failedview, &failedpid) == -ERR_FAULT &&
              !failedview && !failedpid && spacelookup(space, 0x7c000, &leaf) &&
              leaf.pa == original.pa && leaf.prot == original.prot);
      pgstat(&after);
      REQUIRE(staged.allocated == after.allocated && !setstat(set, &group) && group.instances == 1);
      failed++;
      count = 0;
    } else {
      count = mode == 4 ? 3 : 1;
      for (i = 0; i < count; i++) {
        REQUIRE(!procforkbuild(set, &source, &context, 1, 1, &views[i], &children[i]) &&
                !procinfo(children[i], &child[i]) && child[i].state == PROC_CONSTRUCT &&
                child[i].configured &&
                !localtarget(&source, views[i], PREP_CONFIG, &preparations[i], &failedpid) &&
                failedpid == children[i] && !prepstat(preparations[i], &prep) &&
                prep.origin == PREP_FORK && !prep.cloning && prep.state == PREP_ACTIVE &&
                !prephold(preparations[i]));
        REQUIRE(!lifestat(child[i].life, &childlife) && childlife.parent == parentpid &&
                childlife.cred.ruid == cred.ruid && childlife.cred.euid == cred.euid &&
                childlife.cred.rgid == cred.rgid && childlife.cred.egid == cred.egid &&
                childlife.group == parent.group && !childlife.waitref &&
                !acctstat(child[i].acct, &account) && account.limit == 64 &&
                account.exec_limit == 64);
        REQUIRE(!spacestat(child[i].space, &tree) && tree.refs == 3 &&
                !imagestat(child[i].identity, &identity) && identity.reserve == 1 &&
                !kstacktop(child[i].stack, &top));
        childuser = (Hctx *) (top - sizeof(Hctx));
        REQUIRE(hal_syscall_arg(childuser, 0) == 0 && hal_syscall_arg(childuser, 1) == 0 &&
                hal_syscall_arg(childuser, 2) == (u64) children[i] &&
                hal_syscall_arg(childuser, 3) == 0);
        REQUIRE(!localnative(proctable(children[i]), selfspace) &&
                !localnative(proctable(children[i]), selfalloc) &&
                !localnative(proctable(children[i]), codecap) &&
                !localnative(proctable(children[i]), stackcap) &&
                !localcreator(proctable(children[i]), createcap, &inherited) &&
                inherited.rights == CREATE_USE);
        REQUIRE(procconfigure(children[i], NULL, 0) == -ERR_STATE &&
                procpublish(children[i]) == -ERR_STATE &&
                localstart(&source, views[i]) == -ERR_STATE);
        accepted++;
      }
      if (mode == 4) {
        pgstat(&staged);
        failedview = failedpid = 77;
        REQUIRE(procforkbuild(set, &source, &context, 1, 1, &failedview, &failedpid) ==
                    -ERR_CAPACITY &&
                !failedview && !failedpid && !setstat(set, &group) && group.instances == 4);
        pgstat(&after);
        REQUIRE(staged.allocated == after.allocated);
        failed++;
      }
      if (mode == 1) {
        REQUIRE(!prepstat(preparations[0], &prep));
        for (j = 0; j <= PREPS / 8; j++)
          REQUIRE(prepexpire(prep.deadline, 8) >= 0);
        REQUIRE(!prepstat(preparations[0], &prep) && prep.expired && prep.state == PREP_ABORTED &&
                preppublishcheck(preparations[0]) == -ERR_TIMEDOUT);
      } else if (mode == 2) {
        REQUIRE(!localclose(&source) && !prepstat(preparations[0], &prep) &&
                prep.state == PREP_ABORTED);
      } else
        for (i = 0; i < count; i++)
          REQUIRE(!localdestroy(&source, views[i]));
      for (i = 0; i < count; i++) {
        REQUIRE(!lifestat(child[i].life, &childlife) && childlife.state == LIFE_ABORTED &&
                !childlife.waitref);
        clean(children[i]);
        REQUIRE(procinfo(children[i], &stale) == -ERR_STALE);
        sweep(preparations[i]);
        REQUIRE(!prepstat(preparations[i], &prep) && prep.state == PREP_ABORTED && !prep.target &&
                !prepdrop(preparations[i]));
        REQUIRE(prepstat(preparations[i], &prep) == -ERR_STALE);
        cancelled++;
      }
    }
    REQUIRE(!imageend(image, IMAGE_EXIT) && !sigdiscard(image) && !sigrelease(image) &&
            !imagedetach(image) && !localclose(&source));
    while ((error = localdrain(&source, 32)) > 0) {
    }
    REQUIRE(!error);
    while (spaceclear(space, 32) > 0) {
    }
    REQUIRE(!memfree(code) && !memfree(stack) && !spacedestroy(space) &&
            !lifeend(parentlife, LIFE_EXIT, 42, NULL) && !lifedetach(parentlife));
    REQUIRE(!lifestat(parentlife, &parent) && parent.waitref);
    {
      Liferesult result;
      REQUIRE(!lifecollect(parentlife, 1, &result));
    }
    REQUIRE(!acctclose(acct) && !acctunbind(acct));
    cowpool(&pool);
    REQUIRE(!pool.branches && !pool.groups && !pool.prepaid && !pgreserved() && !sigcheck() &&
            !cowcheck() && !acctcheck() && !pgcheck() && !lifecheck() && !setstat(set, &group) &&
            !group.charged && !group.instances && !setclose(set));
  }
  parentdeath();
  accepted++;
  cancelled++;
  pgstat(&after);
  REQUIRE(before.free == after.free && before.allocated == after.allocated);
  putstr("FORKLIFECHECK cases=");
  puthex(6);
  putstr(" prepared=");
  puthex(accepted);
  putstr(" cancelled=");
  puthex(cancelled);
  putstr(" failed=");
  puthex(failed);
  putstr(" expired=");
  puthex(1);
  putstr(" owner_close=");
  puthex(1);
  putstr(" parent_kill=");
  puthex(1);
  putstr(" charged=");
  puthex(0);
  putstr(" allocated=");
  puthex(after.allocated);
  putstr("\n");
}
