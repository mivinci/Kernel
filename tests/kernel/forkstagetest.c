#include "ktest.h"
#include <local.h>
#include <cow.h>
#include <diag.h>
#include <vm.h>
static Local  entries[2][16];
static Rtable tables[2];
#define REQUIRE(c)                                                                                 \
  do {                                                                                             \
    if (!(c)) {                                                                                    \
      putstr("FORKSTAGEFAIL line=");                                                               \
      puthex(__LINE__);                                                                            \
      putstr("\n");                                                                                \
      panic("fork target-table staging");                                                          \
    }                                                                                              \
  } while (0)
static void
opaque(Rtable *table)
{
  Queryrec records[4];
  u32      written;
  u64      next;
  REQUIRE(!localquery(table, 0, records, 4, &written, &next) && !written && !next);
}
static void
bindview(u32 index, Memid view)
{
  REQUIRE(!localforkbind(&tables[0], &tables[1], index, view));
}
static u64
word(Memid memory)
{
  u64 pa;
  REQUIRE(!mempage(memory, 0, &pa));
  return *(volatile u64 *) kaddr(pa);
}
void
forkstagetest(void)
{
  Setid        set;
  Set          group;
  Acctid       accounts[2];
  Acct         account;
  Spaceid      spaces[2];
  Lifeid       lives[2];
  Life         life;
  Imageid      images[2];
  Image        image;
  Handle       pids[2], self, allocator, data, alias, code, shared, creator, call, guessed;
  Memid        original, sealed, common, views[3];
  Mem          state;
  Createpolicy policy, inherited;
  Createarg    limits;
  Liferesult   result;
  Pgstat       before, after;
  Cowpool      pool;
  u64          generations[16], value;
  u32          mode, i, extra, hidden = 0, finished = 0, dropped = 0, overflow = 0, abandoned = 0;
  int          error;
  pgstat(&before);
  for (mode = 0; mode < 4; mode++) {
    REQUIRE(!setcreate(256, 256, 4, &set));
    for (i = 0; i < 2; i++) {
      pids[i] = (2 + (6000 + mode * 2 + i) * 64 + (i + 1));
      REQUIRE(!acctcreate(set, 32, 32, &accounts[i]) && !spacecreate(accounts[i], &spaces[i]) &&
              !lifecreaterun(set, pids[i], i ? pids[0] : 1, i + 1, &lives[i]) &&
              !lifebindacct(lives[i], accounts[i]) && !imagecreate(lives[i], 0, &images[i]) &&
              !localinit(&tables[i], entries[i], 16, accounts[i], spaces[i]));
      tables[i].owner = pids[i];
      tables[i].image = 1;
    }
    for (i = 0; i < 16; i++)
      entries[1][i].generation = mode == 1 ? HANDLE_GENERATION_MAX : 800 + i;
    REQUIRE(!localspace(&tables[0], 3, &self) && !localallocator(&tables[0], 7, &allocator) &&
            !memcreate(accounts[0], 2 * PAGE_SIZE, &original));
    value = 0x4141;
    REQUIRE(!memwrite(original, 0, &value, sizeof(value)) &&
            !localmem(&tables[0], original, 3, 0, 2 * PAGE_SIZE, LOCAL_FORK, &data) &&
            !localmem(&tables[0], original, 1, PAGE_SIZE, PAGE_SIZE, LOCAL_FORK, &alias) &&
            !memfree(original) && !memcode(accounts[0], PAGE_SIZE, &sealed) && !memseal(sealed) &&
            !localmem(&tables[0], sealed, 5, 0, PAGE_SIZE, LOCAL_FORK, &code) && !memfree(sealed) &&
            !memshared(accounts[0], PAGE_SIZE, &common) &&
            !localmem(&tables[0], common, 3, 0, PAGE_SIZE, LOCAL_FORK, &shared) &&
            !memfree(common));
    policy.set = set;
    policy.memlimit = policy.exec_limit = 32;
    policy.resources = 16;
    policy.pending = 1;
    policy.rights = CREATE_RIGHTS | LOCAL_GRANT;
    REQUIRE(!localcreate(&tables[0], &policy, &creator) && !lifepublish(lives[0]) &&
            !imageactivate(images[0]) &&
            !localendpoint(&tables[0], images[0], LOCAL_CALL, LOCAL_USE, 0, LOCAL_FORK, &call));
    /* Stage functions retain managers in the target. These leaf creation
       references are returned before any caller could hand off its C frame. */
    REQUIRE(!memforkclone(original, accounts[1], &views[0]));
    bindview(handleindex(alias), views[0]);
    REQUIRE(!memfree(views[0]) && !memimmutable(sealed, accounts[1], &views[1]));
    bindview(handleindex(code), views[1]);
    REQUIRE(!memfree(views[1]));
    views[2] = common;
    bindview(handleindex(shared), common);
    for (i = 0; i < 16; i++)
      generations[i] = entries[1][i].generation;
    guessed =
        handlepack(HANDLE_RESOURCE, entries[1][handleindex(alias)].generation, handleindex(alias));
    REQUIRE(localnative(&tables[1], guessed) == -ERR_BAD_HANDLE);
    opaque(&tables[1]);
    hidden++;
    limits.memlimit = limits.exec_limit = 32;
    limits.resources = 16;
    limits.pending = 1;
    limits.ruid = limits.euid = limits.rgid = limits.egid = 0;
    limits.reserved = 0;
    if (mode == 0) {
      value = 0x4242;
      REQUIRE(!memwrite(views[0], 0, &value, sizeof(value)) && word(original) == 0x4141 &&
              !localforkdrop(&tables[1]) && !memstat(original, &state) && !state.fork);
      dropped++;
    } else if (mode == 2) {
      REQUIRE(!imagestat(images[0], &image));
      extra = 65535 - image.cleanup;
      for (i = 0; i < extra; i++)
        REQUIRE(!imagehold(images[0], IMAGE_CLEANUP));
      REQUIRE(localforkload(&tables[0], &tables[1], &limits) == -ERR_OVERFLOW &&
              !imagestat(images[0], &image) && image.cleanup == 65535 &&
              !memstat(original, &state) && !state.fork);
      for (i = 0; i < extra; i++)
        REQUIRE(!imagedrop(images[0], IMAGE_CLEANUP));
      overflow++;
    } else {
      REQUIRE(!localforkload(&tables[0], &tables[1], &limits));
      for (i = 0; i < 16; i++)
        REQUIRE(entries[1][i].generation == generations[i]);
      REQUIRE(localnative(&tables[1], guessed) == -ERR_BAD_HANDLE &&
              localnative(&tables[1], data) == -ERR_BAD_HANDLE);
      opaque(&tables[1]);
      hidden++;
      REQUIRE(!localforkreserve(&tables[0], &tables[1], images[1], 1));
      if (mode == 1) {
        for (i = 0; i < 3; i++)
          REQUIRE(!memforkcommit(views[i]));
        REQUIRE(!localforkfinish(&tables[0], &tables[1]));
        for (i = 0; i < 16; i++)
          REQUIRE(entries[1][i].generation == entries[0][i].generation &&
                  !(entries[1][i].source & LOCAL_STAGED));
        REQUIRE(!localnative(&tables[1], self) && !localnative(&tables[1], allocator) &&
                !localnative(&tables[1], data) && !localnative(&tables[1], alias) &&
                !localnative(&tables[1], code) && !localnative(&tables[1], shared) &&
                !localnative(&tables[1], call) && !localcreator(&tables[1], creator, &inherited) &&
                inherited.rights == CREATE_USE && !imagestat(images[1], &image) &&
                image.reserve == 1);
        finished++;
      } else {
        REQUIRE(!imageend(images[0], IMAGE_EXIT) && !localclose(&tables[0]));
        while ((error = localdrain(&tables[0], 1)) > 0) {
        }
        REQUIRE(!error && !spacedestroy(spaces[0]) && !acctclose(accounts[0]) &&
                !acctunbind(accounts[0]) && !acctstat(accounts[0], &account) &&
                account.state == AC_CLOSING);
        REQUIRE(!imageend(images[1], IMAGE_ABORT) && !localclose(&tables[1]));
        while ((error = localdrain(&tables[1], 1)) > 0) {
        }
        REQUIRE(!error && acctstat(accounts[0], &account) == AC_BAD);
        for (i = 0; i < 3; i++)
          REQUIRE(memstat(views[i], &state) == MEM_BAD);
        abandoned++;
      }
    }
    if (mode == 0 || mode == 2)
      for (i = 0; i < 16; i++)
        REQUIRE(!entries[1][i].subtype && entries[1][i].generation == generations[i]);
    for (i = 0; i < 2; i++) {
      REQUIRE(!imageend(images[i], i ? IMAGE_ABORT : IMAGE_EXIT) && !localclose(&tables[i]));
      while ((error = localdrain(&tables[i], 1)) > 0) {
      }
      REQUIRE(!error);
      if (mode != 3 || i)
        REQUIRE(!spacedestroy(spaces[i]) && !acctclose(accounts[i]) && !acctunbind(accounts[i]));
      REQUIRE(!imagedetach(images[i]));
      if (i)
        REQUIRE(!lifecancel(lives[i]));
      else
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
  REQUIRE(before.free == after.free && before.allocated == after.allocated);
  putstr("FORKSTAGECHECK cases=");
  puthex(4);
  putstr(" hidden=");
  puthex(hidden);
  putstr(" finished=");
  puthex(finished);
  putstr(" dropped=");
  puthex(dropped);
  putstr(" overflow=");
  puthex(overflow);
  putstr(" abandoned=");
  puthex(abandoned);
  putstr(" charged=");
  puthex(0);
  putstr(" allocated=");
  puthex(after.allocated);
  putstr("\n");
}
