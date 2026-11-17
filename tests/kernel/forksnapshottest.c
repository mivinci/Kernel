#include "ktest.h"
#include <fork.h>
#include <signal.h>
#include <cow.h>
#include <diag.h>
static Local  slots[2][16];
static Rtable tables[2];
void          arch_forktrap(Hctx *context);
#define REQUIRE(c)                                                                                 \
  do {                                                                                             \
    if (!(c)) {                                                                                    \
      putstr("FORKSNAPFAIL line=");                                                                \
      puthex(__LINE__);                                                                            \
      putstr("\n");                                                                                \
      panic("unified fork snapshot");                                                              \
    }                                                                                              \
  } while (0)
static u64
checksum(const Hctx *context)
{
  const u8 *p = (const u8 *) context;
  u64       hash = 14695981039346656037UL;
  u32       i;
  for (i = 0; i < sizeof(*context); i++)
    hash = (hash ^ p[i]) * 1099511628211UL;
  return hash;
}
void
forksnapshottest(void)
{
  Setid      set;
  Set        group;
  Acctid     accounts[2];
  Spaceid    spaces[2];
  Lifeid     lives[2];
  Imageid    images[2];
  Handle     pids[2], handles[8];
  Memid      code, stack, data, immutable, shared;
  Mem        backing;
  Life       life;
  Liferesult result;
  Createarg  limits;
  Sigact     action;
  Siginfo    info;
  Sigstat    signal;
  Sighead    prefix;
  Hctx       user, child;
  Hinit      initial;
  Hpte       original, leaf;
  Pgstat     before, staged, after;
  Cowpool    pool;
  u64        hash, frames[2], cookies[2], value;
  u32        mode, i, j, rejected = 0;
  int        error;
  pgstat(&before);
  for (mode = 0; mode < 4; mode++) {
    REQUIRE(!setcreate(256, 256, 4, &set));
    for (i = 0; i < 2; i++) {
      pids[i] = handlepack(HANDLE_PROCESS, 4000 + mode * 2 + i, i + 1);
      REQUIRE(!acctcreate(set, 64, 64, &accounts[i]) && !spacecreate(accounts[i], &spaces[i]) &&
              !lifecreate(set, pids[i], i ? pids[0] : 1, &lives[i]) &&
              !lifebindacct(lives[i], accounts[i]) && !imagecreate(lives[i], 0, &images[i]) &&
              !sigreserve(images[i]) &&
              !localinit(&tables[i], slots[i], 16, accounts[i], spaces[i]));
      tables[i].owner = pids[i];
      tables[i].image = 1;
    }
    REQUIRE(!memcode(accounts[0], PAGE_SIZE, &code) && !memseal(code) &&
            !spacemap(spaces[0], code, 0, 0x10000, PAGE_SIZE, VM_READ | VM_EXEC) &&
            !memcreate(accounts[0], 5 * PAGE_SIZE, &stack) &&
            !spacemap(spaces[0], stack, 0, 0x7c000, 5 * PAGE_SIZE, VM_READ | VM_WRITE) &&
            !memcreate(accounts[0], PAGE_SIZE, &data) &&
            !memcode(accounts[0], PAGE_SIZE, &immutable) && !memseal(immutable) &&
            !memshared(accounts[0], PAGE_SIZE, &shared));
    REQUIRE(!localspace(&tables[0], 3, &handles[0]) &&
            !localallocator(&tables[0], 7, &handles[1]) &&
            !localmem(&tables[0], code, 5, 0, PAGE_SIZE, LOCAL_FORK, &handles[2]) &&
            !localmem(&tables[0], stack, 3, 0, 5 * PAGE_SIZE, LOCAL_FORK, &handles[3]) &&
            !localmem(&tables[0], data, 3, 0, PAGE_SIZE, LOCAL_FORK, &handles[4]) &&
            !localmem(&tables[0], immutable, 5, 0, PAGE_SIZE, LOCAL_FORK, &handles[5]) &&
            !localmem(&tables[0], shared, 3, 0, PAGE_SIZE, LOCAL_FORK, &handles[6]) &&
            !localmem(&tables[0], data, 1, 0, PAGE_SIZE, LOCAL_FORK, &handles[7]) &&
            !lifepublish(lives[0]) && !imageactivate(images[0]));
    action.kind = SIGNAL_HANDLER;
    action.handler = mode == 2 ? 0x40000 : 0x10080;
    action.trampoline = 0x10040;
    action.mask = action.reserved = 0;
    action.flags = SIGNAL_SIGINFO | SIGNAL_NODEFER;
    REQUIRE(!siginstall(images[0], SIGNAL_USR1, &action));
    initial.entry = 0x10000;
    initial.sp = 0x80040;
    initial.arg = 0x1234;
    hal_user_init(&user, &initial);
    for (i = 0; i < sizeof(info); i++)
      ((u8 *) &info)[i] = 0;
    info.signum = SIGNAL_USR1;
    if (mode == 1)
      for (i = 0; i < 2; i++) {
        REQUIRE(!sigqueue(images[0], &info) && !sigdeliver(images[0], spaces[0], &user));
        frames[i] = hal_user_sp(&user);
        REQUIRE(!spaceread(spaces[0], frames[i], &prefix, sizeof(prefix)));
        cookies[i] = prefix.cookie;
      }
    arch_forktrap(&user);
    hash = checksum(&user);
    for (i = 0; i < sizeof(child); i++)
      ((u8 *) &child)[i] = 0xa5;
    limits.memlimit = limits.exec_limit = 64;
    limits.resources = 16;
    limits.pending = 1;
    limits.ruid = limits.euid = limits.rgid = limits.egid = 0;
    limits.reserved = 0;
    if (mode == 3)
      for (i = 8; i < 16; i++)
        slots[0][i].generation = HANDLE_GENERATION_MAX;
    REQUIRE(spacelookup(spaces[0], 0x7c000, &original));
    pgstat(&staged);
    {
      Prepid none;
      none.generation = none.index = none.reserved = 0;
      error = forksnapshot(&tables[0], &tables[1], &limits, 1, &user, &child, none);
    }
    REQUIRE(checksum(&user) == hash);
    if (mode >= 2) {
      REQUIRE(error == (mode == 2 ? -ERR_FAULT : -ERR_OVERFLOW) &&
              spacelookup(spaces[0], 0x7c000, &leaf) && leaf.pa == original.pa &&
              leaf.prot == original.prot && !memstat(stack, &backing) && !backing.cowmaps);
      for (i = 0; i < sizeof(child); i++)
        REQUIRE(((u8 *) &child)[i] == 0xa5);
      for (i = 0; i < 16; i++)
        REQUIRE(!slots[1][i].subtype);
      REQUIRE(!memstat(data, &backing) && backing.refs == backing.managers);
      pgstat(&after);
      REQUIRE(staged.allocated == after.allocated);
      rejected++;
    } else {
      REQUIRE(!error && !sigstat(images[1], &signal) && signal.depth == (mode ? 2U : 0U));
      for (i = 0; i < 8; i++)
        REQUIRE(!localnative(&tables[1], handles[i]));
      REQUIRE(slots[1][handleindex(handles[4])].object == slots[1][handleindex(handles[7])].object);
      value = 0x4141;
      REQUIRE(!spacewrite(spaces[1], 0x7c000, &value, 8) &&
              !spaceread(spaces[0], 0x7c000, &value, 8) && !value);
      if (mode)
        for (i = 0; i < 2; i++) {
          REQUIRE(!spaceread(spaces[0], frames[i], &prefix, sizeof(prefix)) &&
                  prefix.cookie == cookies[i]);
          REQUIRE(!spaceread(spaces[1], frames[i], &prefix, sizeof(prefix)) &&
                  prefix.cookie != cookies[i]);
        }
      REQUIRE(!lifepublish(lives[1]) && !imageactivate(images[1]));
    }
    for (i = 0; i < 2; i++) {
      REQUIRE(!imageend(images[i], i && mode >= 2 ? IMAGE_ABORT : IMAGE_EXIT));
      REQUIRE(!sigdiscard(images[i]) && !sigrelease(images[i]) && !imagedetach(images[i]) &&
              !localclose(&tables[i]));
      while ((error = localdrain(&tables[i], 32)) > 0) {
      }
      REQUIRE(!error);
    }
    while (spaceclear(spaces[0], 32) > 0) {
    }
    while (spaceclear(spaces[1], 32) > 0) {
    }
    REQUIRE(!memfree(code) && !memfree(stack) && !memfree(data) && !memfree(immutable) &&
            !memfree(shared));
    for (i = 0; i < 2; i++) {
      REQUIRE(!spacedestroy(spaces[i]));
      if (i && mode >= 2)
        REQUIRE(!lifecancel(lives[i]));
      else
        REQUIRE(!lifeend(lives[i], LIFE_EXIT, 42, NULL));
      REQUIRE(!lifedetach(lives[i]));
      if (!lifestat(lives[i], &life) && life.waitref)
        REQUIRE(!lifecollect(lives[i], life.parent, &result));
      REQUIRE(!acctclose(accounts[i]) && !acctunbind(accounts[i]));
      /* Exhausted generations belong to this test lineage, not the next mode. */
      for (j = 0; j < 16; j++)
        slots[i][j].generation = 0;
    }
    cowpool(&pool);
    REQUIRE(!pool.branches && !pool.groups && !pool.prepaid && !pgreserved() && !sigcheck() &&
            !cowcheck() && !acctcheck() && !pgcheck() && !lifecheck() && !setstat(set, &group) &&
            !group.charged && !group.instances && !setclose(set));
  }
  pgstat(&after);
  REQUIRE(before.free == after.free && before.allocated == after.allocated);
  putstr("FORKSNAPCHECK cases=");
  puthex(4);
  putstr(" committed=");
  puthex(2);
  putstr(" rejected=");
  puthex(rejected);
  putstr(" layers=");
  puthex(2);
  putstr(" charged=");
  puthex(0);
  putstr(" allocated=");
  puthex(after.allocated);
  putstr("\n");
}
