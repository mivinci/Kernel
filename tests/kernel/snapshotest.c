#include "ktest.h"
#include <space.h>
#include <cow.h>
#include <diag.h>
#define REQUIRE(c)                                                                                 \
  do {                                                                                             \
    if (!(c)) {                                                                                    \
      putstr("SNAPSHOTFAIL line=");                                                                \
      puthex(__LINE__);                                                                            \
      putstr("\n");                                                                                \
      panic("Space snapshot");                                                                     \
    }                                                                                              \
  } while (0)
void
snapshotest(void)
{
  Setid   set;
  Set     group;
  Acctid  payer, child, limited;
  Acct    beforeacct, afteracct;
  Spaceid parent, copy, failed;
  Space   empty;
  Memid   data, other, code, shared;
  Mem     backing;
  Cowpool pool;
  Pgstat  before, after;
  Hpte    entry, source;
  u64     value, original[2];
  u32     i;
  pgstat(&before);
  REQUIRE(!setcreate(128, 128, 4, &set) && !acctcreate(set, 64, 64, &payer) &&
          !acctcreate(set, 32, 32, &child) && !acctcreate(set, 5, 5, &limited));
  REQUIRE(!spacecreate(payer, &parent) && !spacecreate(child, &copy) &&
          !spacecreate(limited, &failed));
  REQUIRE(!memcreate(payer, 8192, &data) && !memcreate(payer, 4096, &other) &&
          !memcode(payer, 4096, &code) && !memshared(payer, 4096, &shared));
  value = 0x4141;
  REQUIRE(!memwrite(data, 0, &value, 8));
  value = 0x4242;
  REQUIRE(!memwrite(data, 4096, &value, 8));
  value = 0x4343;
  REQUIRE(!memwrite(code, 0, &value, 8) && !memseal(code));
  value = 0x4444;
  REQUIRE(!memwrite(shared, 0, &value, 8));
  REQUIRE(!spacemap(parent, other, 0, 0x50000, 4096, VM_READ | VM_WRITE) &&
          !spacemap(parent, shared, 0, 0x40000, 4096, VM_READ | VM_WRITE) &&
          !spacemap(parent, code, 0, 0x30000, 4096, VM_READ | VM_EXEC) &&
          !spacemap(parent, data, 0, 0x10000, 8192, VM_READ | VM_WRITE) &&
          !spacemap(parent, data, 4096, 0x20000, 4096, VM_READ));
  REQUIRE(!mempage(data, 0, &original[0]) && !mempage(data, 1, &original[1]));
  REQUIRE(!memfree(data) && !memfree(other) && !memfree(code) && !memfree(shared));
  REQUIRE(!acctstat(limited, &beforeacct));
  REQUIRE(spacesnapshot(parent, failed) == AC_LIMIT && !spacestat(failed, &empty) &&
          empty.pages == 1 && !empty.mapcount && !empty.busy && !acctstat(limited, &afteracct) &&
          beforeacct.used == afteracct.used && beforeacct.reserved == afteracct.reserved &&
          beforeacct.refs == afteracct.refs && !pgreserved());
  cowpool(&pool);
  REQUIRE(!pool.branches && !pool.groups && !pool.prepaid);
  for (i = 0; i < 2; i++)
    REQUIRE(spacelookup(parent, 0x10000 + (u64) i * PAGE_SIZE, &entry) &&
            entry.prot == (VM_READ | VM_WRITE | VM_USER) && entry.pa == original[i]);
  REQUIRE(!spacesnapshot(parent, copy) && !spacestat(copy, &empty) && empty.mapcount == 5);
  REQUIRE(!acctstat(child, &afteracct) && afteracct.cow == 3 && afteracct.used == 3);
  REQUIRE(spacelookup(parent, 0x30000, &source) && spacelookup(copy, 0x30000, &entry) &&
          entry.pa == source.pa && entry.prot == (VM_READ | VM_EXEC | VM_USER));
  REQUIRE(!spaceread(copy, 0x30000, &value, 8) && value == 0x4343);
  value = 0x6262;
  REQUIRE(!spacewrite(copy, 0x11000, &value, 8) && !spaceread(copy, 0x20000, &value, 8) &&
          value == 0x6262 && !spaceread(parent, 0x20000, &value, 8) && value == 0x4242);
  value = 0x7373;
  REQUIRE(!spacewrite(copy, 0x40000, &value, 8) && !spaceread(parent, 0x40000, &value, 8) &&
          value == 0x7373);
  value = 0x9999;
  REQUIRE(spacewrite(copy, 0x30000, &value, 8) == SP_BAD);
  while (spaceclear(parent, 32) > 0) {
  }
  REQUIRE(!spacedestroy(parent) && !acctclose(payer) && !acctunbind(payer));
  REQUIRE(!spaceread(copy, 0x10000, &value, 8) && value == 0x4141 &&
          !spaceread(copy, 0x30000, &value, 8) && value == 0x4343);
  REQUIRE(!memstat(code, &backing) && !backing.managers && backing.refs == 1);
  while (spaceclear(copy, 32) > 0) {
  }
  REQUIRE(!spacedestroy(copy) && !spacedestroy(failed) && !acctclose(child) && !acctunbind(child) &&
          !acctclose(limited) && !acctunbind(limited) && acctstat(payer, &afteracct) == AC_BAD);
  cowpool(&pool);
  REQUIRE(!pool.branches && !pool.groups && !pool.prepaid && !pgreserved() &&
          !setstat(set, &group) && !group.charged && !setclose(set) && !cowcheck() && !pgcheck() &&
          !acctcheck());
  pgstat(&after);
  REQUIRE(before.free == after.free && before.allocated == after.allocated);
  putstr("SNAPSHOTCHECK maps=");
  puthex(5);
  putstr(" private=");
  puthex(2);
  putstr(" aliases=");
  puthex(2);
  putstr(" rollback=");
  puthex(1);
  putstr(" charged=");
  puthex(0);
  putstr(" allocated=");
  puthex(after.allocated);
  putstr("\n");
}
