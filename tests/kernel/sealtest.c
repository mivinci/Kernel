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
