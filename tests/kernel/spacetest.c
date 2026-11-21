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

static void
execspacetest(void)
{
  Setid   set;
  Acctid  acct;
  Spaceid old, next;
  Memid   data, code, denied;
  Space   target;
  Set     group;
  Hpte    pte;
  Pgstat  before, after;
  u64     ticket, marker = 0x31415926, value = 0;
  u64     edge = (1UL << 21) - PAGE_SIZE;
  pgstat(&before);
  REQUIRE(!setcreate(4, 9, 1, &set) && !acctcreate(set, 4, 9, &acct) && !spacecreate(acct, &old) &&
              !memcreate(acct, PAGE_SIZE, &data) &&
              !spacemap(old, data, 0, 0x10000, PAGE_SIZE, VM_READ | VM_WRITE) &&
              !spacewrite(old, 0x10000, &marker, sizeof(marker)),
          "exec load old image");
  REQUIRE(!acctexecbegin(acct, &ticket) && spacecreate(acct, &next) == AC_LIMIT &&
              !spaceexeccreate(acct, ticket, &next),
          "exec load root admission");
  denied.generation = 99;
  REQUIRE(memexeccreate(acct, ticket, 5 * PAGE_SIZE, 0, &denied) == AC_LIMIT &&
              denied.generation == 99 && !setstat(set, &group) && group.charged == 5,
          "exec load backing prefix rollback");
  REQUIRE(!memexeccreate(acct, ticket, 2 * PAGE_SIZE, 1, &code) &&
              !memwrite(code, 0, &marker, sizeof(marker)) && !memseal(code),
          "exec load sealed code");
  REQUIRE(spacemap(next, code, 0, edge, PAGE_SIZE, VM_READ | VM_EXEC) == AC_LIMIT &&
              spaceexecmap(next, ticket, code, 0, edge, 2 * PAGE_SIZE, VM_READ | VM_EXEC) ==
                  AC_LIMIT &&
              !spacestat(next, &target) && target.pages == 1 && !target.mapcount &&
              !spacelookup(next, edge, &pte) && !setstat(set, &group) && group.charged == 7,
          "exec load page table prefix rollback");
  REQUIRE(!spaceexecmap(next, ticket, code, 0, edge, PAGE_SIZE, VM_READ | VM_EXEC) &&
              spacelookup(next, edge, &pte) && pte.prot == (VM_READ | VM_EXEC | VM_USER) &&
              !spaceread(next, edge, &value, sizeof(value)) && value == marker &&
              !setstat(set, &group) && group.charged == 9 && !acctcheck(),
          "exec load target map");
  REQUIRE(!acctexecend(acct, ticket) &&
              spaceexecmap(next, ticket, code, PAGE_SIZE, edge - PAGE_SIZE, PAGE_SIZE, VM_READ) ==
                  SP_BAD &&
              !spacelookup(next, edge - PAGE_SIZE, &pte) && !spaceunmap(next, edge) &&
              !memfree(code) && !spacedestroy(next) && !setstat(set, &group) &&
              group.charged == 4 && !spaceread(old, 0x10000, &value, sizeof(value)) &&
              value == marker,
          "exec load cancellation and old image preservation");
  REQUIRE(!spaceunmap(old, 0x10000) && !memfree(data) && !spacedestroy(old) && !acctclose(acct) &&
              !acctunbind(acct) && !setclose(set) && !acctcheck() && !pgcheck(),
          "exec load cleanup");
  pgstat(&after);
  REQUIRE(before.free == after.free && before.allocated == after.allocated,
          "exec load physical conservation");
}

static void
replacetest(Hvs *kernel)
{
  Pgblk         frames[2];
  Pgstat        before, mapped, current, after;
  Hpte          expected, wrong;
  volatile u64 *alias, *direct[2];
  u64           target = KIO + (1UL << 21), tables = kernel->pages, oldvalue;
  u32           i, next, previous = 0;
  pgstat(&before);
  REQUIRE(!pgalloc(0, NULL, &frames[0]) && !pgalloc(0, NULL, &frames[1]),
          "PTE replacement backing");
  direct[0] = kaddr(frames[0].pa);
  direct[1] = kaddr(frames[1].pa);
  direct[0][0] = 0x11111111;
  direct[1][0] = 0x22222222;
  REQUIRE(!hal_vm_map(kernel, target, frames[0].pa, VM_READ | VM_WRITE, VM_NORMAL),
          "PTE replacement map");
  hal_vm_commit(kernel);
  alias = (volatile u64 *) target;
  REQUIRE(*alias == direct[0][0], "PTE initial translation");
  *alias = 0x33333333;
  pgstat(&mapped);
  for (i = 0; i < 64; i++) {
    next = 1 - previous;
    REQUIRE(hal_vm_lookup(kernel, target, &expected), "PTE old lookup");
    wrong = expected;
    wrong.pa = frames[next].pa;
    REQUIRE(hal_vm_replace(kernel, target, &wrong, frames[next].pa, VM_READ) == VM_EXISTS,
            "PTE stale snapshot rejection");
    oldvalue = direct[previous][0];
    REQUIRE(!hal_vm_replace(kernel, target, &expected, frames[next].pa,
                            i & 1 ? VM_READ | VM_WRITE : VM_READ),
            "PTE checked replace");
    REQUIRE(*alias == direct[next][0], "PTE stale physical translation");
    if (i & 1) {
      *alias = 0x44440000 + i;
      REQUIRE(direct[next][0] == 0x44440000 + i, "PTE new physical write");
    }
    REQUIRE(direct[previous][0] == oldvalue, "PTE old backing modified");
    pgstat(&current);
    REQUIRE(current.free == mapped.free && current.allocated == mapped.allocated,
            "PTE replace allocated tables");
    previous = next;
  }
  REQUIRE(hal_vm_lookup(kernel, target, &expected) &&
              !hal_vm_replace(kernel, target, &expected, expected.pa, expected.prot),
          "PTE no-op");
  REQUIRE(!hal_vm_unmap(kernel, target) && !pgfree(frames[0]) && !pgfree(frames[1]) &&
              kernel->pages == tables && !pgcheck(),
          "PTE replacement cleanup");
  pgstat(&after);
  REQUIRE(before.free == after.free && before.allocated == after.allocated,
          "PTE physical conservation");
  putstr("PTEREPLACECHECK replaced=");
  puthex(64);
  putstr(" allocated=");
  puthex(after.allocated);
  putstr("\n");
}
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
  replacetest(kernel);
  execspacetest();
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
