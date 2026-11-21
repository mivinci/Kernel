#include "ktest.h"
#include <signal.h>
#include <cow.h>
#include <diag.h>
#define REQUIRE(c)                                                                                 \
  do {                                                                                             \
    if (!(c)) {                                                                                    \
      putstr("SIGFORKFAIL line=");                                                                 \
      puthex(__LINE__);                                                                            \
      putstr("\n");                                                                                \
      panic("fork signal inheritance");                                                            \
    }                                                                                              \
  } while (0)
static void
copyuser(Hctx *out, const Hctx *in)
{
  u32 i;
  for (i = 0; i < sizeof(*out); i++)
    ((u8 *) out)[i] = ((const u8 *) in)[i];
}
static void
sigexectest(void)
{
  Setid      set;
  Acctid     acct;
  Lifeid     owner;
  Imageid    old, next, wrong;
  Spaceid    space;
  Memid      code, stack;
  Life       life;
  Liferesult result;
  Sigact     handler = {SIGNAL_HANDLER, 0x10080, 0x10040, 0, SIGNAL_SIGINFO, 0};
  Sigact     action, child;
  Siginfo    info = {0}, received;
  Sigstat    state;
  Sighead    first, second;
  Hctx       user;
  Hinit      initial;
  Pgstat     before, after;
  u32        mode;
  u64        frame;
  u64        usr1 = (u64) 1 << (SIGNAL_USR1 - 1), usr2 = (u64) 1 << (SIGNAL_USR2 - 1);
  Handle     pid = handlepack(HANDLE_PROCESS, 3010, 1);
  pgstat(&before);
  REQUIRE(!setcreate(64, 64, 1, &set) && !acctcreate(set, 64, 64, &acct) &&
          !spacecreate(acct, &space) && !lifecreate(set, pid, 1, &owner) &&
          !lifebindacct(owner, acct) && !imagecreate(owner, 0, &old) && !sigreserve(old) &&
          !lifepublish(owner) && !imageactivate(old));
  REQUIRE(!memcode(acct, PAGE_SIZE, &code) && !memseal(code) &&
          !spacemap(space, code, 0, 0x10000, PAGE_SIZE, VM_READ | VM_EXEC) &&
          !memcreate(acct, PAGE_SIZE, &stack) &&
          !spacemap(space, stack, 0, 0x80000, PAGE_SIZE, VM_READ | VM_WRITE));
  initial.entry = 0x10000;
  initial.sp = 0x81000;
  initial.arg = 0;
  for (mode = 0; mode < 2; mode++) {
    child = (Sigact){0};
    child.kind = mode ? SIGNAL_IGNORE : SIGNAL_DEFAULT;
    child.flags = SIGNAL_NOCLDWAIT | SIGNAL_NOCLDSTOP;
    REQUIRE(!siginstall(old, SIGNAL_CHLD, &child) && !siginstall(old, SIGNAL_USR1, &handler) &&
            !siginstall(old, SIGNAL_USR2, &handler) &&
            !sigsetmask(old, SIGNAL_SETMASK, usr2, &frame));
    hal_user_init(&user, &initial);
    info.signum = SIGNAL_USR1;
    REQUIRE(!sigqueue(old, &info) && !sigdeliver(old, space, &user));
    frame = hal_user_sp(&user);
    REQUIRE(!spaceread(space, frame, &first, sizeof(first)));
    info.signum = SIGNAL_USR2;
    info.status = 0x1234 + mode;
    REQUIRE(!sigqueue(old, &info) && !sigstat(old, &state) && state.depth == 1 &&
            state.mask == (usr1 | usr2) && state.pending == usr2 &&
            !imagecreate(owner, 0, &wrong) && !imagecreate(owner, state.image, &next) &&
            sigexec(old, wrong) == -ERR_STATE && !sigstat(old, &state) && state.depth == 1 &&
            state.pending == usr2 && !lifestat(owner, &life) &&
            (life.child_policy & LIFE_AUTOCHILD));
    REQUIRE(!sigrestore(old, space, &user, frame) && !sigstat(old, &state) && !state.depth &&
            state.mask == usr2 && state.pending == usr2);
    info.signum = SIGNAL_USR1;
    REQUIRE(!sigqueue(old, &info) && !sigdeliver(old, space, &user) &&
            hal_user_sp(&user) == frame && !spaceread(space, frame, &first, sizeof(first)));
    REQUIRE(!imageend(wrong, IMAGE_ABORT) && !imagedetach(wrong) && !sigexec(old, next) &&
            sigstat(old, &state) == -ERR_STALE && !imageend(old, IMAGE_EXEC) && !imagedetach(old) &&
            !imageactivate(next) && !sigstat(next, &state) && !state.depth &&
            state.mask == (usr1 | usr2) && state.pending == usr2 && !sigready(next) &&
            sigrestore(next, space, &user, frame) == -ERR_STATE);
    REQUIRE(!siggetaction(next, SIGNAL_USR1, &action) && action.kind == SIGNAL_DEFAULT &&
            !action.handler && !action.trampoline && !action.mask && !action.flags &&
            !siggetaction(next, SIGNAL_CHLD, &action) && action.kind == child.kind &&
            !action.flags && !lifestat(owner, &life) &&
            life.child_policy == (mode ? LIFE_AUTOCHILD | LIFE_IGNORECHLD : 0));
    hal_user_init(&user, &initial);
    REQUIRE(!sigsetmask(next, SIGNAL_UNBLOCK, usr2, &frame) && sigready(next) == 1 &&
            !siginstall(next, SIGNAL_USR2, &handler) && !sigdeliver(next, space, &user));
    frame = hal_user_sp(&user);
    REQUIRE(!spaceread(space, frame, &second, sizeof(second)) && second.cookie > first.cookie &&
            !spaceread(space, frame + sizeof(Sighead), &received, sizeof(received)) &&
            received.signum == SIGNAL_USR2 && received.status == info.status &&
            !sigrestore(next, space, &user, frame));
    old = next;
  }
  REQUIRE(!imageend(old, IMAGE_EXIT) && !sigdiscard(old) && !sigrelease(old) && !imagedetach(old) &&
          !spaceclear(space, 32) && !memfree(code) && !memfree(stack) && !spacedestroy(space) &&
          !lifeend(owner, LIFE_EXIT, 42, NULL) && !lifedetach(owner) &&
          !lifecollect(owner, 1, &result) && !acctclose(acct) && !acctunbind(acct) &&
          !setclose(set) && !sigcheck() && !lifecheck() && !acctcheck() && !pgcheck());
  pgstat(&after);
  REQUIRE(before.free == after.free && before.allocated == after.allocated);
}
void
sigforktest(void)
{
  Setid      set;
  Set        group;
  Acctid     accounts[2];
  Spaceid    spaces[2];
  Lifeid     lives[2];
  Imageid    images[2];
  Handle     pids[2];
  Life       life;
  Liferesult result;
  Memid      code, stack;
  Sigact     action, inherited;
  Siginfo    info;
  Sigstat    state, parentstate;
  Sighead    original[2], copied[2];
  Hctx       sourceuser, childuser;
  Hinit      initial;
  Hpte       entry;
  Pgstat     before, after;
  Cowpool    pool;
  u64        frames[2], old, mask, childcookie;
  u32        mode, i, layers = 0, rejected = 0;
  int        error;
  sigexectest();
  pgstat(&before);
  for (mode = 0; mode < 2; mode++) {
    REQUIRE(!setcreate(128, 128, 4, &set));
    for (i = 0; i < 2; i++) {
      pids[i] = handlepack(HANDLE_PROCESS, 3000 + mode * 2 + i, 1 + i);
      REQUIRE(!acctcreate(set, 32, 32, &accounts[i]) && !spacecreate(accounts[i], &spaces[i]) &&
              !lifecreate(set, pids[i], i ? pids[0] : 1, &lives[i]) &&
              !lifebindacct(lives[i], accounts[i]) && !imagecreate(lives[i], 0, &images[i]) &&
              !sigreserve(images[i]));
    }
    REQUIRE(!memcode(accounts[0], PAGE_SIZE, &code) && !memseal(code) &&
            !spacemap(spaces[0], code, 0, 0x10000, PAGE_SIZE, VM_READ | VM_EXEC));
    REQUIRE(!memcreate(accounts[0], 5 * PAGE_SIZE, &stack) &&
            !spacemap(spaces[0], stack, 0, 0x7c000, 5 * PAGE_SIZE, VM_READ | VM_WRITE));
    REQUIRE(!lifepublish(lives[0]) && !imageactivate(images[0]));
    action.kind = SIGNAL_HANDLER;
    action.handler = 0x10080;
    action.trampoline = 0x10040;
    action.mask = 0;
    action.flags = SIGNAL_SIGINFO | SIGNAL_NODEFER;
    action.reserved = 0;
    REQUIRE(!siginstall(images[0], SIGNAL_USR1, &action));
    action.kind = SIGNAL_IGNORE;
    action.handler = action.trampoline = 0;
    action.flags = 0;
    REQUIRE(!siginstall(images[0], SIGNAL_TERM, &action));
    action.kind = SIGNAL_DEFAULT;
    action.flags = SIGNAL_NOCLDWAIT;
    REQUIRE(!siginstall(images[0], SIGNAL_CHLD, &action));
    initial.entry = 0x10000;
    initial.sp = 0x80040;
    initial.arg = 0x1234;
    hal_user_init(&sourceuser, &initial);
    mask = (u64) 1 << (mode ? SIGNAL_USR2 - 1 : SIGNAL_USR1 - 1);
    REQUIRE(!sigsetmask(images[0], SIGNAL_SETMASK, mask, &old));
    for (i = 0; i < sizeof(info); i++)
      ((u8 *) &info)[i] = 0;
    info.signum = SIGNAL_USR1;
    info.status = 0x4141;
    if (mode) {
      for (i = 0; i < 2; i++) {
        REQUIRE(!sigqueue(images[0], &info) && !sigdeliver(images[0], spaces[0], &sourceuser));
        frames[i] = hal_user_sp(&sourceuser);
        REQUIRE(!spaceread(spaces[0], frames[i], &original[i], sizeof(Sighead)));
      }
    }
    REQUIRE(!sigqueue(images[0], &info) && !sigstat(images[0], &parentstate) &&
            parentstate.pending == ((u64) 1 << (SIGNAL_USR1 - 1)));
    REQUIRE(!spacesnapshot(spaces[0], spaces[1]) &&
            !sigfork(images[0], spaces[0], images[1], spaces[1]));
    REQUIRE(sigfork(images[0], spaces[0], images[1], spaces[1]) == -ERR_STATE &&
            siginitial(images[1], 0, 0) == -ERR_STATE &&
            sigbaseline(images[1], images[0]) == -ERR_STATE);
    REQUIRE(!sigstat(images[1], &state) && state.mask == mask && !state.pending &&
            state.depth == (mode ? 2U : 0U));
    REQUIRE(!lifestat(lives[1], &life) && life.child_policy == LIFE_AUTOCHILD);
    if (mode) {
      for (i = 0; i < 2; i++) {
        REQUIRE(!spaceread(spaces[1], frames[i], &copied[i], sizeof(Sighead)) &&
                copied[i].cookie != original[i].cookie && copied[i].depth == original[i].depth &&
                copied[i].previous == original[i].previous &&
                copied[i].restore_mask == original[i].restore_mask);
        /* Read the full prefix into its own type; parent cookie stays original. */
        {
          Sighead head;
          REQUIRE(!spaceread(spaces[0], frames[i], &head, sizeof(head)) &&
                  head.cookie == original[i].cookie);
        }
      }
      REQUIRE(copied[0].cookie != copied[1].cookie && spacelookup(spaces[1], 0x80000, &entry) &&
              entry.prot == (VM_READ | VM_USER | VM_COW));
    }
    REQUIRE(!lifepublish(lives[1]) && !imageactivate(images[1]));
    REQUIRE(!siggetaction(images[1], SIGNAL_USR1, &inherited) && inherited.kind == SIGNAL_HANDLER &&
            inherited.handler == 0x10080 && inherited.trampoline == 0x10040 &&
            inherited.flags == (SIGNAL_SIGINFO | SIGNAL_NODEFER));
    REQUIRE(!siggetaction(images[1], SIGNAL_TERM, &inherited) && inherited.kind == SIGNAL_IGNORE);
    copyuser(&childuser, &sourceuser);
    if (mode) {
      childcookie = copied[1].cookie;
      copied[1].cookie = original[1].cookie;
      REQUIRE(!spacewrite(spaces[1], frames[1], &copied[1], sizeof(Sighead)));
      error = sigrestore(images[1], spaces[1], &childuser, frames[1]);
      REQUIRE(error == -ERR_INVALID && !sigstat(images[1], &state) && state.depth == 2);
      rejected++;
      REQUIRE(!spaceread(spaces[1], frames[0], &copied[0], sizeof(Sighead)));
      copied[1].cookie = childcookie;
      REQUIRE(!spacewrite(spaces[1], frames[1], &copied[1], sizeof(Sighead)) &&
              !sigrestore(images[1], spaces[1], &childuser, frames[1]) &&
              !sigrestore(images[1], spaces[1], &childuser, frames[0]));
      REQUIRE(childuser.tf.pc == 0x10000 && hal_user_sp(&childuser) == 0x80040 &&
              hal_syscall_arg(&childuser, 0) == 0x1234 && spacelookup(spaces[1], 0x80000, &entry) &&
              entry.prot == (VM_READ | VM_USER | VM_COW));
      REQUIRE(!sigstat(images[0], &state) && state.depth == 2 &&
              state.pending == parentstate.pending);
      REQUIRE(!sigrestore(images[0], spaces[0], &sourceuser, frames[1]) &&
              !sigrestore(images[0], spaces[0], &sourceuser, frames[0]));
      layers += 2;
    }
    REQUIRE(!sigstat(images[1], &state) && !state.depth && !state.pending && state.mask == mask);
    for (i = 0; i < 2; i++)
      REQUIRE(!imageend(images[i], IMAGE_EXIT) && !sigdiscard(images[i]) &&
              !sigrelease(images[i]) && !imagedetach(images[i]));
    while (spaceclear(spaces[0], 32) > 0) {
    }
    while (spaceclear(spaces[1], 32) > 0) {
    }
    REQUIRE(!memfree(code) && !memfree(stack));
    for (i = 0; i < 2; i++) {
      REQUIRE(!spacedestroy(spaces[i]) && !lifeend(lives[i], LIFE_EXIT, 42, NULL) &&
              !lifedetach(lives[i]));
      if (!lifestat(lives[i], &life) && life.waitref)
        REQUIRE(!lifecollect(lives[i], life.parent, &result));
      REQUIRE(!acctclose(accounts[i]) && !acctunbind(accounts[i]));
    }
    cowpool(&pool);
    REQUIRE(!pool.branches && !pool.groups && !pool.prepaid && !pgreserved() && !sigcheck() &&
            !cowcheck() && !acctcheck() && !pgcheck() && !lifecheck() && !setstat(set, &group) &&
            !group.charged && !group.instances && !setclose(set));
  }
  pgstat(&after);
  REQUIRE(before.free == after.free && before.allocated == after.allocated);
  putstr("SIGFORKCHECK cases=");
  puthex(2);
  putstr(" layers=");
  puthex(layers);
  putstr(" rejected=");
  puthex(rejected);
  putstr(" cow_sp=");
  puthex(1);
  putstr(" pending=");
  puthex(0);
  putstr(" charged=");
  puthex(0);
  putstr(" allocated=");
  puthex(after.allocated);
  putstr("\n");
}
