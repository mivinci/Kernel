/* Trusted mapping/snapshot exercise, not production PID1 or native fork. */
#include "ktest.h"
#include <bootstore.h>
#include <abi/boot.h>
#include <local.h>
#include <vm.h>
#include <diag.h>

static Rtable table;
static Local  slots[1];
#define REQUIRE(c)                                                                                 \
  do {                                                                                             \
    if (!(c)) {                                                                                    \
      putstr("BOOTVIEWFAIL line=");                                                                \
      puthex(__LINE__);                                                                            \
      putstr("\n");                                                                                \
      panic("retained boot mappings");                                                             \
    }                                                                                              \
  } while (0)

static void
check(Spaceid root, const Bootstore *store)
{
  Hpte      entry;
  u8        data[256];
  const u8 *source = kaddr(store->base);
  u64       i, j, pa, offset = store->base & PAGE_MASK, position;
  for (i = 0; i < store->pages; i++) {
    REQUIRE(!bootstorepage(i, &pa));
    REQUIRE(spacelookup(root, BOOT_PACKAGE_LO + i * PAGE_SIZE, &entry) && entry.pa == pa &&
            entry.prot == (VM_READ | VM_USER) && entry.attr == VM_NORMAL);
  }
  for (i = 0; i < store->pages * PAGE_SIZE; i += sizeof(data)) {
    REQUIRE(!spaceread(root, BOOT_PACKAGE_LO + i, data, sizeof(data)));
    for (j = 0; j < sizeof(data); j++) {
      position = i + j;
      REQUIRE(
          data[j] ==
          (position >= offset && position - offset < store->bytes ? source[position - offset] : 0));
    }
  }
}

static void
clear(Spaceid root)
{
  int error;
  do {
    error = spaceclear(root, 32);
  } while (error == 1);
  REQUIRE(!error);
}

void
bootviewtest(void)
{
  Bootstore initial, state;
  Pgstat    before, after;
  Setid     set;
  Set       total;
  Acctid    a, b;
  Acct      account, previous;
  Spaceid   parent, child;
  Memid     memory, invalid;
  Handle    handle;
  u64       first, pages, chunks = 0;
  u8        byte = 0x7b;
  REQUIRE(!bootstorestat(&initial) && initial.bytes && !initial.refs);
  pgstat(&before);
  REQUIRE(!setcreate(4096, 8192, 8, &set));
  REQUIRE(!acctcreate(set, 1024, 1024, &a) && !acctcreate(set, 1024, 1024, &b));
  REQUIRE(!spacecreate(a, &parent) && !spacecreate(b, &child));
  REQUIRE(!localinit(&table, slots, 1, a, parent));
  invalid.generation = 77;
  invalid.index = invalid.reserved = 77;
  REQUIRE(memboot(a, 0, MEM_PAGES + 1, &invalid) == MEM_BAD && invalid.generation == 77 &&
          invalid.index == 77 && invalid.reserved == 77);
  for (first = 0; first < initial.pages; first += pages) {
    pages = MIN(MEM_PAGES, initial.pages - first);
    REQUIRE(!acctstat(a, &previous));
    REQUIRE(!memboot(a, (u32) first, (u32) pages, &memory));
    REQUIRE(!acctstat(a, &account) && account.used == previous.used &&
            account.reserved == previous.reserved && account.refs == previous.refs + 1);
    if (!first) {
      REQUIRE(memcanmap(memory, VM_READ | VM_WRITE) == MEM_BAD);
      REQUIRE(memcanmap(memory, VM_READ | VM_EXEC) == MEM_BAD);
      REQUIRE(memwrite(memory, 0, &byte, 1) == MEM_BAD && memseal(memory) == MEM_BAD);
      REQUIRE(localmem(&table, memory, VM_READ, 0, pages * PAGE_SIZE, 0, &handle) == -ERR_DENIED &&
              !handle);
    }
    REQUIRE(!spacemap(parent, memory, 0, BOOT_PACKAGE_LO + first * PAGE_SIZE, pages * PAGE_SIZE,
                      VM_READ));
    REQUIRE(!memfree(memory));
    chunks++;
  }
  REQUIRE(!localclose(&table) && !localdrain(&table, 1));
  REQUIRE(spacewrite(parent, BOOT_PACKAGE_LO, &byte, 1) < 0);
  REQUIRE(spacecopy(parent, BOOT_PACKAGE_LO, BOOT_PACKAGE_LO + 1, 1) < 0);
  check(parent, &initial);
  REQUIRE(!bootstorestat(&state) && state.refs == chunks && state.retained == initial.retained);
  REQUIRE(!spacesnapshot(parent, child));
  check(child, &initial);
  REQUIRE(!bootstorestat(&state) && state.refs == chunks);
  clear(parent);
  REQUIRE(!spacedestroy(parent) && !acctclose(a) && !acctunbind(a));
  REQUIRE(!acctstat(a, &account) && account.state == AC_CLOSING && !account.used &&
          !account.reserved);
  check(child, &initial);
  clear(child);
  REQUIRE(!bootstorestat(&state) && !state.refs && state.retained == initial.retained);
  REQUIRE(acctstat(a, &account) == AC_BAD);
  REQUIRE(!spacedestroy(child) && !acctclose(b) && !acctunbind(b));
  REQUIRE(!setstat(set, &total) && !total.charged && !total.instances && !total.leases);
  REQUIRE(!setclose(set));
  pgstat(&after);
  REQUIRE(after.allocated == before.allocated && after.free == before.free && !pgcheck() &&
          !acctcheck());
  putstr("BOOTVIEW chunks=");
  puthex(chunks);
  putstr(" pages=");
  puthex(initial.pages);
  putstr(" snapshots=");
  puthex(1);
  putstr(" denied=");
  puthex(7);
  putstr(" refs=");
  puthex(state.refs);
  putstr(" charged=");
  puthex(0);
  putstr(" allocated=");
  puthex(after.allocated);
  putstr("\n");
}
