#include "ktest.h"
#include <space.h>
#include <diag.h>
#include <vm.h>
#define REQUIRE(condition, reason)                                                                 \
  do {                                                                                             \
    if (!(condition))                                                                              \
      panic(reason);                                                                               \
  } while (0)

void
memtest(Hvs *kernel)
{
  Setid         group;
  Acctid        acct;
  Spaceid       space;
  Memid         dirty, memory;
  Mem           backing;
  Space         mapped;
  Acct          account;
  Set           set;
  Hpte          pte;
  Pgstat        before, baseline, after;
  u64           pa, reused, quota, output[2], observed[2];
  u32           i;
  volatile u64 *data;
  UNUSED(kernel);
  pgstat(&before);
  REQUIRE(!setcreate(4096, 8192, 8, &group) && !acctcreate(group, 4096, 8192, &acct),
          "memory account");
  REQUIRE(!memcreate(acct, PAGE_SIZE, &dirty) && !mempage(dirty, 0, &pa), "initial data backing");
  data = kaddr(pa);
  for (i = 0; i < PAGE_SIZE / sizeof(u64); i++)
    data[i] = ~(u64) 0;
  REQUIRE(!memfree(dirty) && !memcreate(acct, PAGE_SIZE, &memory) && !mempage(memory, 0, &reused),
          "data reuse");
  REQUIRE(reused == pa && memory.index == dirty.index && memory.generation > dirty.generation &&
              memfree(dirty) == MEM_BAD,
          "data identity reuse");
  data = kaddr(reused);
  for (i = 0; i < PAGE_SIZE / sizeof(u64); i++)
    REQUIRE(!data[i], "data zeroing");
  REQUIRE(!memfree(memory) && !spacecreate(acct, &space) &&
              !memcreate(acct, 4 * PAGE_SIZE, &memory),
          "mapping preparation");
  REQUIRE(!acctstat(acct, &account), "memory fees");
  quota = 4096 - account.used - 2;
  REQUIRE(!acctreserve(acct, quota), "mapping failure preparation");
  pgstat(&baseline);
  REQUIRE(spacemap(space, memory, 0, 0x1ff000, 4 * PAGE_SIZE, VM_READ | VM_WRITE) == AC_LIMIT,
          "mapping failure injection");
  REQUIRE(!spacestat(space, &mapped) && mapped.pages == 1 && !mapped.busy && mapped.maps == PG_NONE,
          "mapping publication rollback");
  REQUIRE(!memstat(memory, &backing) && backing.refs == 1 && backing.managers == 1,
          "mapping reference rollback");
  for (i = 0; i < 4; i++)
    REQUIRE(!spacelookup(space, 0x1ff000 + (u64) i * PAGE_SIZE, &pte), "partial mapping survived");
  pgstat(&after);
  REQUIRE(after.free == baseline.free && after.allocated == baseline.allocated &&
              !acctrelease(acct, 0, quota),
          "mapping physical rollback");
  REQUIRE(!spacemap(space, memory, 0, 0x1ff000, 4 * PAGE_SIZE, VM_READ | VM_WRITE) &&
              !spacemap(space, memory, 0, 0x400000, 4 * PAGE_SIZE, VM_READ),
          "data aliases");
  for (i = 0; i < 4; i++) {
    REQUIRE(!mempage(memory, i, &pa), "mapped data page");
    data = kaddr(pa);
    REQUIRE(!data[0] && !data[PAGE_SIZE / sizeof(u64) - 1], "mapped data clear");
    data[0] = pa ^ 0x12345678;
    REQUIRE(spacelookup(space, 0x1ff000 + (u64) i * PAGE_SIZE, &pte) && pte.pa == pa &&
                pte.prot == (VM_READ | VM_WRITE | VM_USER),
            "data mapping permissions");
    REQUIRE(spacelookup(space, 0x400000 + (u64) i * PAGE_SIZE, &pte) && pte.pa == pa &&
                pte.prot == (VM_READ | VM_USER),
            "read alias permissions");
  }
  output[0] = 0x1122334455667788UL;
  output[1] = 0xaabbccddeeff0011UL;
  REQUIRE(!spacewrite(space, 0x1ffff8, output, sizeof(output)) &&
              !spaceread(space, 0x400ff8, observed, sizeof(observed)) && observed[0] == output[0] &&
              observed[1] == output[1],
          "kernel output aliases");
  REQUIRE(spacewrite(space, 0x400ff8, output, sizeof(output)) == SP_BAD &&
              spacewrite(space, 0x202ff8, output, sizeof(output)) == SP_BAD &&
              !mempage(memory, 3, &pa),
          "kernel output permissions");
  data = kaddr(pa);
  REQUIRE(!data[PAGE_SIZE / sizeof(u64) - 1], "partial kernel output survived");
  REQUIRE(!memfree(memory) && !memstat(memory, &backing) && backing.managers == 0 &&
              backing.refs == 2 && backing.state == MEM_DATA,
          "mapping backing retention");
  REQUIRE(!acctclose(acct) && !acctunbind(acct), "mapped account close");
  REQUIRE(spaceunmap(space, 0x200000) == SP_BAD && spacedestroy(space) == SP_BUSY,
          "mapping exact start");
  REQUIRE(!spaceunmap(space, 0x1ff000) && !memstat(memory, &backing) && backing.refs == 1,
          "independent mapping lifetime");
  REQUIRE(spacelookup(space, 0x400000, &pte) && pte.present, "last alias lost");
  REQUIRE(!spaceunmap(space, 0x400000) && memstat(memory, &backing) == MEM_BAD &&
              !spacestat(space, &mapped) && mapped.pages == 1 && !spacedestroy(space),
          "final backing cleanup");
  REQUIRE(acctstat(acct, &account) == AC_BAD && !setstat(group, &set) && !set.charged &&
              !setclose(group),
          "memory account cleanup");
  pgstat(&after);
  REQUIRE(after.free == before.free && after.allocated == before.allocated && !acctcheck() &&
              !pgcheck(),
          "memory conservation");
  putstr("MEMCHECK pages=");
  puthex(4);
  putstr(" aliases=");
  puthex(2);
  putstr(" charged=");
  puthex(set.charged);
  putstr(" free=");
  puthex(after.free);
  putstr(" allocated=");
  puthex(after.allocated);
  putstr("\n");
}
