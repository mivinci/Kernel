/* Trusted mapping/snapshot exercise, not production PID1 or native fork. */
#include "ktest.h"
#include <bootstore.h>
#include <abi/boot.h>
#include <local.h>
#include <vm.h>
#include <diag.h>
#include <bootpkg.h>
#include <bootimage.h>
#include <hal/boot.h>
#include "load.h"

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

static Handle
bootresourceat(Rtable *resources, Spaceid root)
{
  Hpte  entry;
  Memid memory;
  u64   pa;
  u32   i;
  REQUIRE(spacelookup(root, BOOT_PACKAGE_HI, &entry));
  for (i = 0; i < resources->count; i++) {
    const Local *slot = &resources->slots[i];
    if (slot->subtype != LOCAL_MEM)
      continue;
    memory.generation = slot->objectgen;
    memory.index = slot->object;
    memory.reserved = 0;
    if (!mempage(memory, 0, &pa) && pa == entry.pa)
      return handlepack(HANDLE_RESOURCE, slot->generation, i);
  }
  panic("boot fixture resource");
}

static void
claims(const Bootstore *store)
{
  Bootdir         directory;
  Bootimage       image;
  const u8       *data = kaddr(store->base), *descriptor;
  const Bootitem *item;
  Boot            boot;
  Config          config;
  Setid           set;
  Set             total;
  Handle          pid, startup;
  Procinfo        process;
  Rtable         *resources;
  Memid           substitute;
  Bootstore       state;
  Pgstat          before, after;
  u64             i, first, pages, pa, denied = 0;
  int             error;
  pgstat(&before);
  REQUIRE(!bootpkgcheck(data, store->bytes, hal_boot_machine(), &directory));
  item = &directory.item[directory.header.init_index];
  descriptor = data + item->load_off;
  REQUIRE(!bootimagecheck(descriptor, item->load_bytes, &image));
  REQUIRE(!setcreate(4096, 8192, 8, &set));
  REQUIRE(!procnew(set, 1, 1024, 1024, LOCALS, &pid) && !procinfo(pid, &process));
  REQUIRE(!spacebootmap(process.space));
  for (i = 0; i < sizeof(boot); i++)
    ((u8 *) &boot)[i] = 0;
  boot.magic = BOOT_MAGIC;
  boot.format = BOOT_FORMAT;
  boot.bytes = sizeof(boot);
  boot.user_lo = BOOT_USER_LO;
  boot.user_hi = BOOT_USER_HI;
  boot.package_va = BOOT_PACKAGE_LO + (store->base & PAGE_MASK);
  boot.package_bytes = store->bytes;
  testload(pid, descriptor, item->load_bytes, &boot, sizeof(boot));
  config.entry = image.entry;
  config.stack = 0x3fffff0000UL;
  config.boot = BOOT_PACKAGE_HI;
  config.bootsz = sizeof(boot);
  config.sigmask = config.sigdefault = 0;
  resources = proctable(pid);
  REQUIRE(resources);
  startup = bootresourceat(resources, process.space);
  boot.package_va++;
  REQUIRE(!localwrite(resources, startup, 0, &boot, sizeof(boot)));
  REQUIRE(procconfigure(pid, &config, 0) == -ERR_INVALID && procpublish(pid) == -ERR_INVALID);
  denied++;
  boot.package_va--;
  boot.package_bytes--;
  REQUIRE(!localwrite(resources, startup, 0, &boot, sizeof(boot)));
  REQUIRE(procconfigure(pid, &config, 0) == -ERR_INVALID);
  denied++;
  boot.package_bytes++;
  REQUIRE(!localwrite(resources, startup, 0, &boot, sizeof(boot)));
  REQUIRE(!spaceunmap(process.space, BOOT_PACKAGE_LO));
  REQUIRE(procconfigure(pid, &config, 0) == -ERR_INVALID && procpublish(pid) == -ERR_INVALID);
  denied++;
  pages = MIN(MEM_PAGES, store->pages);
  REQUIRE(!memcreate(process.acct, pages * PAGE_SIZE, &substitute));
  for (i = 0; i < pages; i++) {
    REQUIRE(!bootstorepage(i, &pa));
    REQUIRE(!memwrite(substitute, i * PAGE_SIZE, kaddr(pa), PAGE_SIZE));
  }
  REQUIRE(!spacemap(process.space, substitute, 0, BOOT_PACKAGE_LO, pages * PAGE_SIZE, VM_READ));
  REQUIRE(!memfree(substitute));
  REQUIRE(procconfigure(pid, &config, 0) == -ERR_INVALID);
  denied++;
  REQUIRE(!spaceunmap(process.space, BOOT_PACKAGE_LO));
  if (store->pages > pages) {
    REQUIRE(!memboot(process.acct, 1, (u32) pages, &substitute));
    REQUIRE(!spacemap(process.space, substitute, 0, BOOT_PACKAGE_LO, pages * PAGE_SIZE, VM_READ));
    REQUIRE(!memfree(substitute));
    REQUIRE(procconfigure(pid, &config, 0) == -ERR_INVALID);
    denied++;
    REQUIRE(!spaceunmap(process.space, BOOT_PACKAGE_LO));
  }
  for (first = MEM_PAGES; first < store->pages; first += MEM_PAGES)
    REQUIRE(!spaceunmap(process.space, BOOT_PACKAGE_LO + first * PAGE_SIZE));
  REQUIRE(!spacebootmap(process.space) && !procconfigure(pid, &config, 0));
  REQUIRE(!procdestroy(pid));
  do {
    error = procclean(pid);
  } while (error > 0);
  REQUIRE(!error);
  REQUIRE(!setstat(set, &total) && !total.charged && !total.instances && !total.leases);
  REQUIRE(!setclose(set));
  REQUIRE(!bootstorestat(&state) && !state.refs && state.retained == store->retained);
  pgstat(&after);
  REQUIRE(after.allocated == before.allocated && after.free == before.free);
  putstr("BOOTCLAIM denied=");
  puthex(denied);
  putstr(" configured=");
  puthex(2);
  putstr(" publish_denied=");
  puthex(2);
  putstr(" refs=");
  puthex(state.refs);
  putstr(" allocated=");
  puthex(after.allocated);
  putstr("\n");
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
  claims(&initial);
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
