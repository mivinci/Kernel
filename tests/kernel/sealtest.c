#include "ktest.h"
#include <space.h>
#include <diag.h>
#include <vm.h>
extern const u8 code_begin[], code_end[];
#define REQUIRE(c)                                                                                 \
  do {                                                                                             \
    if (!(c))                                                                                      \
      panic("code sealing");                                                                       \
  } while (0)
static void
immutabletest(Hvs *kernel)
{
  Setid   set;
  Set     group;
  Acctid  payer, child, grandchild;
  Acct    account;
  Spaceid a, b;
  Memid   code, view, last;
  Mem     state;
  Pgstat  before, baseline, after;
  Hpte    entry;
  u64     pa, result, target = KIO + 0x400000;
  pgstat(&before);
  REQUIRE(!setcreate(64, 64, 4, &set) && !acctcreate(set, 16, 16, &payer) &&
          !acctcreate(set, 16, 16, &child) && !acctcreate(set, 16, 16, &grandchild));
  REQUIRE(!memcode(payer, PAGE_SIZE, &code) &&
          !memwrite(code, 0, code_begin, (u64) (code_end - code_begin)) && !memseal(code));
  pgstat(&baseline);
  REQUIRE(!memimmutable(code, child, &view) && !memimmutable(view, grandchild, &last));
  pgstat(&after);
  REQUIRE(after.free == baseline.free && after.allocated == baseline.allocated && !pgreserved());
  REQUIRE(!memstat(last, &state) && state.kind == MEM_IMMUTABLE && state.state == MEM_SEALED &&
          state.backing.generation == code.generation && state.backing.index == code.index);
  REQUIRE(!memfree(view) && !acctclose(child) && !acctunbind(child) &&
          acctstat(child, &account) == AC_BAD);
  REQUIRE(!spacecreate(payer, &a) && !spacecreate(grandchild, &b));
  REQUIRE(!spacemap(a, code, 0, 0x10000, PAGE_SIZE, VM_READ | VM_EXEC) &&
          !spacemap(b, last, 0, 0x10000, PAGE_SIZE, VM_READ | VM_EXEC));
  REQUIRE(!memfree(code) && !spaceunmap(a, 0x10000) && !spacedestroy(a) && !acctclose(payer) &&
          !acctunbind(payer));
  REQUIRE(!acctstat(payer, &account) && account.state == AC_CLOSING && account.used == 1 &&
          !account.bound);
  REQUIRE(!mempage(last, 0, &pa) && spacelookup(b, 0x10000, &entry) && entry.pa == pa &&
          entry.prot == (VM_READ | VM_EXEC | VM_USER) && entry.attr == VM_NORMAL);
  REQUIRE(memwrite(last, 0, code_begin, 4) == MEM_BAD &&
          memcanmap(last, VM_READ | VM_WRITE) == MEM_BAD && !memseal(last));
  REQUIRE(!hal_vm_map(kernel, target, pa, VM_READ | VM_EXEC, VM_NORMAL));
  hal_vm_commit(kernel);
  result = ((u64(*)(void)) target)();
  REQUIRE(result == 42 && !hal_vm_unmap(kernel, target));
  REQUIRE(!memfree(last) && !spaceunmap(b, 0x10000) && !spacedestroy(b) &&
          memstat(code, &state) == MEM_BAD && acctstat(payer, &account) == AC_BAD);
  REQUIRE(!acctclose(grandchild) && !acctunbind(grandchild) && !setstat(set, &group) &&
          !group.charged && !setclose(set) && !acctcheck() && !pgcheck());
  pgstat(&after);
  REQUIRE(before.free == after.free && before.allocated == after.allocated);
  putstr("IMMUTABLECHECK views=");
  puthex(2);
  putstr(" result=");
  puthex(result);
  putstr(" intermediate=");
  puthex(1);
  putstr(" charged=");
  puthex(0);
  putstr(" allocated=");
  puthex(after.allocated);
  putstr("\n");
}
void
sealtest(Hvs *kernel)
{
  Setid   group;
  Acctid  acct;
  Spaceid space;
  Memid   memory;
  Mem     state;
  Pgstat  before, after;
  Hpte    pte;
  u64     pa, result, target = KIO + 0x400000;
  immutabletest(kernel);
  pgstat(&before);
  REQUIRE(!setcreate(128, 256, 2, &group));
  REQUIRE(!acctcreate(group, 128, 256, &acct));
  REQUIRE(!spacecreate(acct, &space));
  REQUIRE(!memcode(acct, PAGE_SIZE, &memory));
  REQUIRE(!memwrite(memory, 0, code_begin, (u64) (code_end - code_begin)));
  REQUIRE(!spacemap(space, memory, 0, 0x10000, PAGE_SIZE, VM_READ | VM_WRITE));
  REQUIRE(memseal(memory) == MEM_BUSY);
  REQUIRE(!memstat(memory, &state) && state.state == MEM_LOADING && state.writes == 1);
  REQUIRE(!spaceunmap(space, 0x10000));
  REQUIRE(!memseal(memory) && !memseal(memory));
  REQUIRE(memwrite(memory, 0, code_begin, 4) == MEM_BAD);
  REQUIRE(spacemap(space, memory, 0, 0x10000, PAGE_SIZE, VM_READ | VM_WRITE) == MEM_BAD);
  REQUIRE(!spacemap(space, memory, 0, 0x10000, PAGE_SIZE, VM_READ | VM_EXEC));
  REQUIRE(!mempage(memory, 0, &pa));
  REQUIRE(spacelookup(space, 0x10000, &pte) && pte.pa == pa &&
          pte.prot == (VM_READ | VM_EXEC | VM_USER));
  REQUIRE(!hal_vm_map(kernel, target, pa, VM_READ | VM_EXEC, VM_NORMAL));
  hal_vm_commit(kernel);
  result = ((u64(*)(void)) target)();
  REQUIRE(result == 42);
  REQUIRE(!hal_vm_unmap(kernel, target));
  REQUIRE(!memfree(memory) && !memstat(memory, &state) && !state.managers &&
          state.state == MEM_SEALED && state.refs == 1 && state.execs == 1);
  REQUIRE(memhold(memory) == MEM_BAD && memretain(memory) == MEM_BAD &&
          memcanmap(memory, VM_READ | VM_EXEC) == MEM_BAD);
  REQUIRE(!spaceunmap(space, 0x10000));
  REQUIRE(!spacedestroy(space));
  REQUIRE(!acctclose(acct) && !acctunbind(acct) && !setclose(group));
  pgstat(&after);
  REQUIRE(before.free == after.free && before.allocated == after.allocated);
  REQUIRE(!acctcheck() && !pgcheck());
  putstr("SEALCHECK result=");
  puthex(result);
  putstr(" allocated=");
  puthex(after.allocated);
  putstr("\n");
}
