#include "ktest.h"
#include <prep.h>
#include <cow.h>
#include <diag.h>
#include <vm.h>
#include <proc.h>
static Local  entries[2][16];
static Rtable tables[2];
#define REQUIRE(c)                                                                                 \
  do {                                                                                             \
    if (!(c)) {                                                                                    \
      putstr("FORKTREEFAIL line=");                                                                \
      puthex(__LINE__);                                                                            \
      putstr("\n");                                                                                \
      panic("fork tree ending");                                                                   \
    }                                                                                              \
  } while (0)
static u32
leaves(Spaceid root, u32 writable)
{
  Hpte pte;
  u32  alias, page, count = 0;
  for (alias = 0; alias < 3; alias++)
    for (page = 0; page < 16; page++)
      if (spacelookup(root, 0x10000 + (u64) alias * 0x20000 + (u64) page * PAGE_SIZE, &pte) &&
          (!writable || pte.prot & VM_WRITE))
        count++;
  return count;
}
void
forktreetest(void)
{
  Setid      set;
  Set        group;
  Acctid     accounts[2];
  Spaceid    roots[2];
  Space      space;
  Lifeid     lives[2];
  Life       life;
  Imageid    images[2];
  Handle     pids[2];
  Prepid     prep;
  Prepinfo   state;
  Spacetxn  *tree, *again;
  Memid      memory;
  Liferesult result;
  Cowpool    pool;
  Pgstat     before, after;
  u64        value, observed;
  u32        mode, i, steps, total = 0, previous, current, restored, oldrestored, edits = 0;
  int        error;
  pgstat(&before);
  for (mode = 0; mode < 3; mode++) {
    REQUIRE(!setcreate(512, 512, 4, &set));
    for (i = 0; i < 2; i++) {
      pids[i] = handlepack(HANDLE_PROCESS, 7000 + mode * 2 + i, i + 1);
      REQUIRE(!acctcreate(set, 128, 128, &accounts[i]) && !spacecreate(accounts[i], &roots[i]) &&
              !lifecreate(set, pids[i], i ? pids[0] : 1, &lives[i]) &&
              !lifebindacct(lives[i], accounts[i]) && !imagecreate(lives[i], 0, &images[i]) &&
              !localinit(&tables[i], entries[i], 16, accounts[i], roots[i]));
      tables[i].owner = pids[i];
      tables[i].image = 1;
    }
    REQUIRE(!lifepublish(lives[0]) && !imageactivate(images[0]) &&
            !memcreate(accounts[0], 16 * PAGE_SIZE, &memory));
    value = 0x4141;
    REQUIRE(!memwrite(memory, 0, &value, sizeof(value)));
    for (i = 0; i < 3; i++)
      REQUIRE(!spacemap(roots[0], memory, 0, 0x10000 + (u64) i * 0x20000, 16 * PAGE_SIZE,
                        VM_READ | VM_WRITE));
    REQUIRE(!prepforkcreate(&tables[0], &tables[1], lives[1], images[1], &prep) &&
            !prepforktree(prep, &tree) && !spacesnapshotprep(roots[0], roots[1], prep) &&
            !spacestat(roots[0], &space) && space.refs == 2 && !spacestat(roots[1], &space) &&
            space.refs == 3 && !prepstat(prep, &state) && state.refs == 2);
    steps = 0;
    previous = leaves(roots[1], 0);
    oldrestored = leaves(roots[0], 1);
    REQUIRE(previous == 48 && !oldrestored);
    do {
      error = spacesnapshotstep(roots[0], roots[1], mode == 1);
      REQUIRE(error == 0 || error == 1);
      current = leaves(roots[1], 0);
      restored = leaves(roots[0], 1);
      REQUIRE(previous >= current && restored >= oldrestored &&
              previous - current + restored - oldrestored <= 32);
      edits += previous - current + restored - oldrestored;
      previous = current;
      oldrestored = restored;
      steps++;
      REQUIRE(steps < 300);
      if (error) {
        REQUIRE(spacesnapshotstep(roots[0], roots[1], mode != 1) == SP_BAD &&
                spaceactivate(roots[0]) == SP_BUSY && spaceactivate(roots[1]) == SP_BUSY &&
                spacewrite(roots[0], 0x10000, &value, sizeof(value)) == SP_BUSY);
        if (mode == 2 && steps == 1) {
          REQUIRE(!prepcancel(prep) && !spacestat(roots[1], &space) && space.refs == 2 &&
                  prepforkfinish(prep) == -ERR_STATE && !prepforktree(prep, &again) &&
                  again == tree);
        }
      }
    } while (error);
    total += steps;
    REQUIRE(steps > 1 && !tree->phase && !prepstat(prep, &state) && state.refs == 1 &&
            !spacestat(roots[0], &space) && space.refs == 1 && !spacestat(roots[1], &space) &&
            space.refs == (mode == 2 ? 1U : 2U) &&
            prepforkfinish(prep) == (mode == 2 ? -ERR_REVOKED : 0));
    if (mode == 1) {
      REQUIRE(current == 48 && !restored);
      value = 0x4242;
      REQUIRE(!spacewrite(roots[0], 0x10000, &value, sizeof(value)) &&
              !spaceread(roots[1], 0x10000, &observed, sizeof(observed)) && observed == 0x4141);
    } else
      REQUIRE(!current && restored == 48);
    REQUIRE(!prepcancel(prep) && !prepdrop(prep) && !memfree(memory));
    for (i = 0; i < 2; i++) {
      REQUIRE(!imageend(images[i], i ? IMAGE_ABORT : IMAGE_EXIT) && !localclose(&tables[i]));
      while ((error = localdrain(&tables[i], 32)) > 0) {
      }
      REQUIRE(!error);
      while ((error = spaceclear(roots[i], 32)) > 0) {
      }
      REQUIRE(!error && !spacedestroy(roots[i]) && !acctclose(accounts[i]) &&
              !acctunbind(accounts[i]) && !imagedetach(images[i]));
      if (i) {
        REQUIRE(!lifestat(lives[i], &life) && life.state == LIFE_ABORTED);
      } else
        REQUIRE(!lifeend(lives[i], LIFE_EXIT, 42, NULL));
      REQUIRE(!lifedetach(lives[i]));
      if (!lifestat(lives[i], &life) && life.waitref)
        REQUIRE(!lifecollect(lives[i], life.parent, &result));
    }
    cowpool(&pool);
    REQUIRE(!pool.groups && !pool.branches && !pool.prepaid && !pgreserved() && !cowcheck() &&
            !acctcheck() && !pgcheck() && !lifecheck() && !groupcheck() && !setstat(set, &group) &&
            !group.charged && !group.instances && !setclose(set));
  }
  pgstat(&after);
  REQUIRE(before.allocated == after.allocated && before.free == after.free && edits == 192);
  putstr("FORKTREECHECK cases=");
  puthex(3);
  putstr(" aborted=");
  puthex(2);
  putstr(" committed=");
  puthex(1);
  putstr(" cancelled=");
  puthex(1);
  putstr(" steps=");
  puthex(total);
  putstr(" edits=");
  puthex(edits);
  putstr(" bounded=");
  puthex(1);
  putstr(" charged=");
  puthex(0);
  putstr(" allocated=");
  puthex(after.allocated);
  putstr("\n");
}

void
forkbuildtest(void)
{
  Setid      set;
  Set        group;
  Acctid     accounts[2];
  Spaceid    roots[2];
  Space      space;
  Lifeid     lives[2];
  Life       life;
  Imageid    images[2];
  Handle     pids[2];
  Prepid     prep;
  Prepinfo   state;
  Spacetxn  *tree;
  Memid      memory;
  Liferesult result;
  Cowpool    pool;
  Pgstat     before, after;
  u64        value, observed;
  u32        mode, i, steps, total = 0, previous, current, partial = 0, changes = 0;
  int        error;
  pgstat(&before);
  for (mode = 0; mode < 3; mode++) {
    REQUIRE(!setcreate(512, 512, 4, &set));
    for (i = 0; i < 2; i++) {
      pids[i] = handlepack(HANDLE_PROCESS, 8000 + mode * 2 + i, i + 1);
      REQUIRE(!acctcreate(set, mode == 2 && i ? 1 : 128, mode == 2 && i ? 1 : 128, &accounts[i]) &&
              !spacecreate(accounts[i], &roots[i]) &&
              !lifecreate(set, pids[i], i ? pids[0] : 1, &lives[i]) &&
              !lifebindacct(lives[i], accounts[i]) && !imagecreate(lives[i], 0, &images[i]) &&
              !localinit(&tables[i], entries[i], 16, accounts[i], roots[i]));
      tables[i].owner = pids[i];
      tables[i].image = 1;
    }
    REQUIRE(!lifepublish(lives[0]) && !imageactivate(images[0]) &&
            !memcreate(accounts[0], 16 * PAGE_SIZE, &memory));
    value = 0x4141;
    REQUIRE(!memwrite(memory, 0, &value, sizeof(value)));
    for (i = 0; i < 3; i++)
      REQUIRE(!spacemap(roots[0], memory, 0, 0x10000 + (u64) i * 0x20000, 16 * PAGE_SIZE,
                        VM_READ | VM_WRITE));
    REQUIRE(!prepforkcreate(&tables[0], &tables[1], lives[1], images[1], &prep) &&
            !prepforktree(prep, &tree) && !spacesnapshotopen(roots[0], roots[1], prep) &&
            !spacestat(roots[0], &space) && space.refs == 2 && !spacestat(roots[1], &space) &&
            space.refs == 3 && !prepstat(prep, &state) && state.refs == 2);
    previous = steps = 0;
    do {
      error = spacesnapshotbuild(roots[0], roots[1]);
      current = leaves(roots[1], 0) + 48 - leaves(roots[0], 1);
      REQUIRE(current >= previous && current - previous <= 32);
      changes += current - previous;
      previous = current;
      steps++;
      REQUIRE(steps < 300);
      if (mode == 1 && current) {
        partial = current;
        REQUIRE(error == 1 && partial <= 32 && !leaves(roots[1], 0) && !prepcancel(prep) &&
                spacesnapshotbuild(roots[0], roots[1]) == SP_BUSY &&
                spacesnapshotbuild(roots[0], roots[1]) == SP_BAD);
        break;
      }
      if (error == 1) {
        REQUIRE(spacesnapshotstep(roots[0], roots[1], 1) == SP_BAD &&
                spaceactivate(roots[0]) == SP_BUSY && spaceactivate(roots[1]) == SP_BUSY &&
                prepforkfinish(prep) == -ERR_STATE);
      }
    } while (error == 1);
    total += steps;
    if (!mode)
      REQUIRE(!error && current == 96 && !spacesnapshotbuild(roots[0], roots[1]));
    if (mode == 2)
      REQUIRE(error == MEM_LIMIT && current == 48 && !leaves(roots[1], 0) &&
              spacesnapshotbuild(roots[0], roots[1]) == SP_BAD && !prepcancel(prep));
    REQUIRE(!spacesnapshotend(roots[0], roots[1], !mode) && !tree->phase &&
            !prepstat(prep, &state) && state.refs == 1 &&
            prepforkfinish(prep) == (mode ? -ERR_REVOKED : 0));
    if (!mode) {
      value = 0x4242;
      REQUIRE(!spacewrite(roots[0], 0x10000, &value, sizeof(value)) &&
              !spaceread(roots[1], 0x10000, &observed, sizeof(observed)) && observed == 0x4141);
    } else
      REQUIRE(!leaves(roots[1], 0) && leaves(roots[0], 1) == 48);
    REQUIRE(!prepcancel(prep) && !prepdrop(prep) && !memfree(memory));
    for (i = 0; i < 2; i++) {
      REQUIRE(!imageend(images[i], i ? IMAGE_ABORT : IMAGE_EXIT) && !localclose(&tables[i]));
      while ((error = localdrain(&tables[i], 32)) > 0) {
      }
      REQUIRE(!error);
      while ((error = spaceclear(roots[i], 32)) > 0) {
      }
      REQUIRE(!error && !spacedestroy(roots[i]) && !acctclose(accounts[i]) &&
              !acctunbind(accounts[i]) && !imagedetach(images[i]));
      if (i) {
        REQUIRE(!lifestat(lives[i], &life) && life.state == LIFE_ABORTED);
      } else
        REQUIRE(!lifeend(lives[i], LIFE_EXIT, 42, NULL));
      REQUIRE(!lifedetach(lives[i]));
      if (!lifestat(lives[i], &life) && life.waitref)
        REQUIRE(!lifecollect(lives[i], life.parent, &result));
    }
    cowpool(&pool);
    REQUIRE(!pool.groups && !pool.branches && !pool.prepaid && !pgreserved() && !cowcheck() &&
            !acctcheck() && !pgcheck() && !lifecheck() && !groupcheck() && !setstat(set, &group) &&
            !group.charged && !group.instances && !setclose(set));
  }
  pgstat(&after);
  REQUIRE(before.allocated == after.allocated && before.free == after.free && partial &&
          changes == 144 + partial);
  putstr("FORKBUILDCHECK cases=");
  puthex(3);
  putstr(" built=");
  puthex(1);
  putstr(" cancelled=");
  puthex(1);
  putstr(" failed=");
  puthex(1);
  putstr(" steps=");
  puthex(total);
  putstr(" partial=");
  puthex(partial);
  putstr(" changes=");
  puthex(changes);
  putstr(" bounded=");
  puthex(1);
  putstr(" charged=");
  puthex(0);
  putstr(" allocated=");
  puthex(after.allocated);
  putstr("\n");
}

void
forkfreezetest(void)
{
  Setid    set;
  Set      group;
  Acctid   targetacct;
  Acct     account;
  Procinfo process, state;
  Rtable  *table;
  Spaceid  target, alias;
  Space    root;
  Memid    memory;
  Mem      backing;
  Handle   pid, allocator = 0, handle;
  Allocarg request;
  Prepid   none;
  Pgstat   before, after;
  Cowpool  pool;
  u64      value = 0x4141, observed;
  u32      i, steps = 0, deferred = 0;
  int      error;
  pgstat(&before);
  REQUIRE(!setcreate(512, 512, 4, &set) && !procnew(set, 1, 128, 128, 16, &pid) &&
          !procinfo(pid, &process));
  table = proctable(pid);
  REQUIRE(table != NULL);
  for (i = 0; i < table->count; i++)
    if (table->slots[i].subtype == LOCAL_ALLOC)
      allocator = handlepack(HANDLE_RESOURCE, table->slots[i].generation, i);
  REQUIRE(allocator != 0);
  request.bytes = 2 * PAGE_SIZE;
  request.kind = ALLOC_DATA;
  request.align = request.minaddr = request.maxaddr = request.reserved = 0;
  REQUIRE(!localalloc(table, allocator, &request, &handle));
  memory.generation = table->slots[handleindex(handle)].objectgen;
  memory.index = table->slots[handleindex(handle)].object;
  memory.reserved = 0;
  REQUIRE(!localwrite(table, handle, 0, &value, sizeof(value)) &&
          !spacemap(process.space, memory, 0, 0x10000, 2 * PAGE_SIZE, VM_READ | VM_WRITE) &&
          !spacecreate(process.acct, &alias) &&
          !spacemap(alias, memory, 0, 0x10000, 2 * PAGE_SIZE, VM_READ | VM_WRITE) &&
          !acctcreate(set, 128, 128, &targetacct) && !spacecreate(targetacct, &target));
  none.generation = none.index = none.reserved = 0;
  REQUIRE(!spacesnapshotopen(process.space, target, none));
  do {
    error = spacesnapshotbuild(process.space, target);
    REQUIRE(error == 0 || error == 1);
    steps++;
    REQUIRE(steps < 100);
  } while (error);
  REQUIRE(!spacestat(alias, &root) && root.refs == 2 && root.frozen == 1 &&
          spacefrozen(alias) == 1 && spaceactivate(alias) == SP_BUSY &&
          spacewrite(alias, 0x10000, &value, sizeof(value)) == SP_BUSY &&
          spaceunmap(alias, 0x10000) == SP_BUSY);
  /* This is a real unpublished Proc teardown, not a synthetic Life-only end.
     Revocation is immediate; its table/account/stack cleanup waits for undo. */
  REQUIRE(!procdestroy(pid));
  for (i = 0; i < 4; i++) {
    REQUIRE(procclean(pid) == 1 && !procinfo(pid, &state) && state.state == PROC_EXITING &&
            !state.phase && table->state == LOCAL_OPEN && !localnative(table, handle) &&
            !acctstat(process.acct, &account) && account.state == AC_ACTIVE);
    deferred++;
  }
  REQUIRE(procwork(32) >= 0 && procwork(32) >= 0 && !procinfo(pid, &state) && !state.phase &&
          !spacesnapshotend(process.space, target, 0) && !spacestat(alias, &root) &&
          root.refs == 1 && !root.frozen && !spacefrozen(alias));
  for (i = 0; i < 64; i++) {
    error = procclean(pid);
    REQUIRE(error >= 0);
    if (!error)
      break;
  }
  REQUIRE(i < 64 && procinfo(pid, &state) == -ERR_STALE && !acctstat(process.acct, &account) &&
          account.state == AC_CLOSING && account.used && !memstat(memory, &backing) &&
          !backing.managers && !spaceread(alias, 0x10000, &observed, sizeof(observed)) &&
          observed == value && !spaceunmap(alias, 0x10000) && !spacedestroy(alias) &&
          acctstat(process.acct, &account) == AC_BAD && !spacedestroy(target) &&
          !acctclose(targetacct) && !acctunbind(targetacct));
  cowpool(&pool);
  REQUIRE(!pool.groups && !pool.branches && !pool.prepaid && !pgreserved() && !cowcheck() &&
          !acctcheck() && !pgcheck() && !lifecheck() && !groupcheck() && !setstat(set, &group) &&
          !group.charged && !group.instances && !setclose(set));
  pgstat(&after);
  REQUIRE(before.free == after.free && before.allocated == after.allocated);
  putstr("FORKFREEZECHECK cases=");
  puthex(1);
  putstr(" alias_lease=");
  puthex(1);
  putstr(" deferred=");
  puthex(deferred);
  putstr(" retired=");
  puthex(1);
  putstr(" charged=");
  puthex(0);
  putstr(" allocated=");
  puthex(after.allocated);
  putstr("\n");
}
