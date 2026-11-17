#include "ktest.h"
#include <cow.h>
#include <diag.h>
#include <vm.h>
#define REQUIRE(c)                                                                                 \
  do {                                                                                             \
    if (!(c)) {                                                                                    \
      putstr("COWFAIL line=");                                                                     \
      puthex(__LINE__);                                                                            \
      putstr("\n");                                                                                \
      panic("COW page branches");                                                                  \
    }                                                                                              \
  } while (0)
static void
fill(Cowid id)
{
  Cowstat       state;
  volatile u64 *memory;
  u32           i;
  REQUIRE(!cowstat(id, &state));
  memory = kaddr(state.pa);
  for (i = 0; i < PAGE_SIZE / sizeof(u64); i++)
    memory[i] = 0x31415926 ^ i;
}
static void
modify(Cowid id, u32 *split, u32 *exclusive)
{
  Cowstat       before, after;
  volatile u64 *memory;
  u64           pa;
  u32           i;
  REQUIRE(!cowstat(id, &before) && !cowwrite(id, &pa) && !cowstat(id, &after));
  REQUIRE(after.branches == 1 && after.payer && after.pa == pa);
  if (before.branches > 1) {
    REQUIRE(pa != before.pa);
    (*split)++;
  } else {
    REQUIRE(pa == before.pa);
    (*exclusive)++;
  }
  memory = kaddr(pa);
  for (i = 0; i < PAGE_SIZE / sizeof(u64); i++)
    REQUIRE(memory[i] == (0x31415926 ^ i));
  memory[0] ^= 0xfeedbeef;
  REQUIRE(!cowcheck() && !acctcheck() && !pgcheck());
}
void
cowpagetest(void)
{
  Setid         set;
  Set           group;
  Acctid        accounts[9];
  Acct          account;
  Cowid         branches[8], denied;
  Cowstat       state, other;
  Cowpool       pool;
  Acctblk       owned;
  Pgblk         raw;
  Pgstat        before, after;
  volatile u64 *memory;
  u64           chain, next, shared;
  u32           mode, i, j, count, start, split = 0, exclusive = 0;
  int           error;
  pgstat(&before);
  REQUIRE(COW_REFS == 8 && !cowcheck() && !pgreserved());
  for (mode = 0; mode < 4; mode++) {
    chain = 0;
    REQUIRE(!setcreate(32, 32, 16, &set));
    count = mode == 3 ? 8 : 2;
    for (i = 0; i < count; i++)
      REQUIRE(!acctcreate(set, 1, 1, &accounts[i]));
    REQUIRE(!acctalloc(accounts[0], 0, NULL, &owned) && !cowcreate(&owned, &branches[0]));
    REQUIRE(!owned.acct.generation && !owned.block.pa);
    fill(branches[0]);
    for (i = 1; i < count; i++)
      REQUIRE(!cowshare(branches[0], accounts[i], &branches[i]));
    REQUIRE(!cowstat(branches[0], &state));
    shared = state.pa;
    for (i = 0; i < count; i++) {
      REQUIRE(!cowstat(branches[i], &state) && state.pa == shared && state.branches == count);
    }
    start = 0;
    if (mode == 2) {
      REQUIRE(!acctclose(accounts[0]) && !acctunbind(accounts[0]) && !cowdrop(branches[0]));
      REQUIRE(acctstat(accounts[0], &account) == AC_BAD && !pgreserved());
      start = 1;
      modify(branches[1], &split, &exclusive);
    } else if (mode == 3) {
      cowpool(&pool);
      REQUIRE(pool.branches == 8 && pool.groups == 1 && pool.prepaid == 7);
      REQUIRE(!acctcreate(set, 1, 1, &accounts[8]) && !acctalloc(accounts[8], 0, NULL, &owned));
      denied.generation = 99;
      denied.index = denied.reserved = 99;
      REQUIRE(cowcreate(&owned, &denied) == AC_FULL && owned.acct.generation &&
              denied.generation == 99 && cowshare(branches[0], accounts[8], &denied) == AC_FULL);
      REQUIRE(!acctfree(owned) && !acctclose(accounts[8]) && !acctunbind(accounts[8]));
      while (!(error = pgalloc(0, NULL, &raw))) {
        memory = kaddr(raw.pa);
        memory[0] = chain;
        chain = raw.pa;
      }
      REQUIRE(error == PG_NOMEM && pgreserved() == 7);
      for (i = 0; i < count; i++)
        modify(branches[i], &split, &exclusive);
      for (i = 0; i < count; i++) {
        REQUIRE(!cowstat(branches[i], &state));
        for (j = i + 1; j < count; j++)
          REQUIRE(!cowstat(branches[j], &other) && state.pa != other.pa);
      }
      cowpool(&pool);
      REQUIRE(pool.groups == 8 && !pool.prepaid && !pgreserved());
    } else {
      modify(branches[mode], &split, &exclusive);
      REQUIRE(!cowstat(branches[1 - mode], &state) && state.pa == shared);
      memory = kaddr(state.pa);
      for (i = 0; i < PAGE_SIZE / sizeof(u64); i++)
        REQUIRE(memory[i] == (0x31415926 ^ i));
    }
    for (i = start; i < count; i++) {
      REQUIRE(!cowdrop(branches[i]) && cowdrop(branches[i]) == AC_BAD && !acctclose(accounts[i]) &&
              !acctunbind(accounts[i]));
    }
    while (chain) {
      memory = kaddr(chain);
      next = memory[0];
      memory[0] = 0;
      raw.pa = chain;
      raw.order = raw.reserved = 0;
      REQUIRE(!pgfree(raw));
      chain = next;
    }
    cowpool(&pool);
    REQUIRE(!pool.branches && !pool.groups && !pool.prepaid && !cowcheck() && !pgreserved());
    REQUIRE(!setstat(set, &group) && !group.charged && !setclose(set) && !acctcheck() &&
            !pgcheck());
  }
  pgstat(&after);
  REQUIRE(before.allocated == after.allocated && before.free == after.free && split == 9 &&
          exclusive == 2);
  putstr("COWCHECK cases=");
  puthex(4);
  putstr(" split=");
  puthex(split);
  putstr(" exclusive=");
  puthex(exclusive);
  putstr(" full=");
  puthex(1);
  putstr(" charged=");
  puthex(0);
  putstr(" allocated=");
  puthex(after.allocated);
  putstr("\n");
}
