#include "ktest.h"
#include <prep.h>
#include <cow.h>
#include <diag.h>
#include <vm.h>
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
