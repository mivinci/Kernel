#include "ktest.h"
#include <cow.h>
#include <mem.h>
#include <space.h>
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
static void
memcowtest(void)
{
  Setid   set;
  Set     group;
  Acctid  parent, child, third, small;
  Acct    account;
  Spaceid source_space, child_space;
  Memid   source, copy, later, denied;
  Mem     status;
  Cowpool pool;
  Pgstat  before, after;
  Hpte    entry;
  u64     original[2], pa;
  u8      data[64], observed[64];
  u32     i;
  pgstat(&before);
  REQUIRE(!setcreate(128, 128, 8, &set) && !acctcreate(set, 32, 32, &parent) &&
          !acctcreate(set, 32, 32, &child) && !acctcreate(set, 32, 32, &third) &&
          !acctcreate(set, 1, 1, &small));
  REQUIRE(!spacecreate(parent, &source_space) && !spacecreate(child, &child_space));
  REQUIRE(!memcreate(parent, 8192, &source));
  for (i = 0; i < sizeof(data); i++)
    data[i] = 0x5a;
  REQUIRE(!memwrite(source, 0, data, sizeof(data)) && !memwrite(source, 4096, data, sizeof(data)));
  REQUIRE(!mempage(source, 0, &original[0]) && !mempage(source, 1, &original[1]));
  REQUIRE(!spacemap(source_space, source, 0, 0x10000, 8192, VM_READ) &&
          !spacemap(source_space, source, 0, 0x20000, 8192, VM_READ));
  REQUIRE(!memclone(source, child, &copy));
  REQUIRE(memcanmap(copy, VM_READ | VM_WRITE) == MEM_BUSY &&
          memwrite(source, 0, data, sizeof(data)) == MEM_BUSY);
  REQUIRE(!memfree(source) && !memstat(source, &status) && !status.managers);
  REQUIRE(!memclone(source, third, &later));
  denied.generation = 9;
  denied.index = denied.reserved = 9;
  REQUIRE(memclone(source, small, &denied) == AC_LIMIT && denied.generation == 9 &&
          !acctstat(small, &account) && !account.used && !account.reserved && !account.cow &&
          !acctclose(small) && !acctunbind(small));
  REQUIRE(!cowcheck() && !mempage(source, 0, &pa) && pa == original[0]);
  for (i = 0; i < sizeof(data); i++)
    data[i] = 0x63;
  REQUIRE(!memwrite(copy, 0, data, sizeof(data)) && !memwrite(copy, 4096, data, sizeof(data)));
  REQUIRE(!mempage(copy, 0, &pa) && pa != original[0] &&
          !spacemap(child_space, copy, 0, 0x10000, 8192, VM_READ | VM_WRITE));
  REQUIRE(spacelookup(child_space, 0x10000, &entry) && entry.pa == pa &&
          entry.prot == (VM_READ | VM_WRITE | VM_USER) && entry.attr == VM_NORMAL);
  data[0] = 0x77;
  REQUIRE(!spacewrite(child_space, 0x10000, data, sizeof(data)));
  for (i = 0; i < sizeof(data); i++)
    data[i] = 0x6c;
  REQUIRE(!memwrite(later, 0, data, sizeof(data)) && !memwrite(later, 4096, data, sizeof(data)));
  REQUIRE(!spaceread(source_space, 0x10000, observed, sizeof(observed)));
  for (i = 0; i < sizeof(observed); i++)
    REQUIRE(observed[i] == 0x5a);
  REQUIRE(!spaceread(source_space, 0x21000, observed, sizeof(observed)));
  for (i = 0; i < sizeof(observed); i++)
    REQUIRE(observed[i] == 0x5a);
  REQUIRE(!spaceunmap(source_space, 0x10000) && !spaceunmap(source_space, 0x20000) &&
          memstat(source, &status) == MEM_BAD && !spacedestroy(source_space) &&
          !acctclose(parent) && !acctunbind(parent) && acctstat(parent, &account) == AC_BAD);
  REQUIRE(!spaceread(child_space, 0x10000, observed, sizeof(observed)) && observed[0] == 0x77);
  REQUIRE(!memfree(copy) && !spaceunmap(child_space, 0x10000) && !spacedestroy(child_space) &&
          !memfree(later));
  REQUIRE(!acctclose(child) && !acctunbind(child) && !acctclose(third) && !acctunbind(third));
  cowpool(&pool);
  REQUIRE(!pool.branches && !pool.groups && !pool.prepaid && !pgreserved() &&
          !setstat(set, &group) && !group.charged && !setclose(set) && !cowcheck() &&
          !acctcheck() && !pgcheck());
  pgstat(&after);
  REQUIRE(before.free == after.free && before.allocated == after.allocated);
  putstr("MEMCOWCHECK clones=");
  puthex(2);
  putstr(" rejected=");
  puthex(1);
  putstr(" aliases=");
  puthex(2);
  putstr(" charged=");
  puthex(0);
  putstr(" allocated=");
  puthex(after.allocated);
  putstr("\n");
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
  memcowtest();
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
