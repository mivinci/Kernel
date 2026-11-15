#include "ktest.h"
#include <boot.h>
#include <space.h>
#include <diag.h>
#include <vm.h>

#define REQUIRE(condition, reason)                                                                 \
  do {                                                                                             \
    if (!(condition))                                                                              \
      panic(reason);                                                                               \
  } while (0)
static Spaceid roots[SPACES];

void
spacetest(Hvs *kernel)
{
  Setid         group;
  Acctid        acct;
  Spaceid       old, fresh, denied;
  Space         snapshot;
  Acct          account;
  Set           set;
  Acctblk       dirty;
  Pgstat        before, after;
  Hpte          entry;
  u32           i;
  volatile u64 *memory;
  const u64    *parent = kaddr(kernel->root);
  pgstat(&before);
  REQUIRE(!setcreate(512, 1024, 8, &group) && !acctcreate(group, 512, 1024, &acct), "root account");
  REQUIRE(!acctalloc(acct, 0, NULL, &dirty), "dirty root preparation");
  memory = kaddr(dirty.block.pa);
  for (i = 0; i < 512; i++)
    memory[i] = ~(u64) 0;
  REQUIRE(!acctfree(dirty) && !spacecreate(acct, &old) && !spacestat(old, &snapshot),
          "root creation");
  REQUIRE(snapshot.root == dirty.block.pa && snapshot.pages == 1, "zeroed root reuse");
  memory = kaddr(snapshot.root);
  for (i = 0; i < 256; i++)
    REQUIRE(!memory[i], "dirty lower user root");
  for (i = 256; i < 512; i++)
    REQUIRE(memory[i] == parent[i], "kernel root sharing");
  REQUIRE(!spacelookup(old, 0x10000, &entry), "fresh user mapping");
  REQUIRE(spacelookup(old, (u64) __text_start, &entry) && entry.prot == (VM_READ | VM_EXEC) &&
              entry.pa == kphysical(__text_start),
          "kernel user permissions");
  REQUIRE(!acctstat(acct, &account) && account.used == 1 && account.refs == 3, "root charge");
  REQUIRE(!spacehold(old) && spacedestroy(old) == SP_BUSY && !spacedrop(old) && !spacedestroy(old),
          "root reference");
  REQUIRE(!spacecreate(acct, &fresh) && fresh.index == old.index &&
              fresh.generation > old.generation && spacedestroy(old) == SP_BAD &&
              !spacedestroy(fresh),
          "root generation");
  for (i = 0; i < SPACES; i++)
    REQUIRE(!spacecreate(acct, &roots[i]), "root pool allocation");
  denied.generation = 99;
  denied.index = 99;
  denied.reserved = 99;
  REQUIRE(spacecreate(acct, &denied) == SP_FULL && denied.generation == 99 && denied.index == 99,
          "root pool exhaustion");
  REQUIRE(!acctstat(acct, &account) && account.used == SPACES && !account.reserved,
          "root pool fees");
  REQUIRE(!acctclose(acct) && !acctunbind(acct), "root account close");
  REQUIRE(!acctstat(acct, &account) && account.state == AC_CLOSING && account.used == SPACES,
          "root account retention");
  for (i = 0; i < SPACES; i++)
    REQUIRE(!spacedestroy(roots[i]), "root reclaim");
  REQUIRE(acctstat(acct, &account) == AC_BAD && !acctcheck() && !pgcheck(),
          "root account retirement");
  REQUIRE(!setstat(group, &set) && set.charged == 0 && set.refs == 1 && !set.instances &&
              !setclose(group),
          "root set cleanup");
  pgstat(&after);
  REQUIRE(before.free == after.free && before.allocated == after.allocated,
          "user root physical cleanup");
  putstr("SPACECHECK created=");
  puthex(SPACES);
  putstr(" charged=");
  puthex(set.charged);
  putstr(" free=");
  puthex(after.free);
  putstr(" allocated=");
  puthex(after.allocated);
  putstr("\n");
}
