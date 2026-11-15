#include "ktest.h"
#include <local.h>
#include <diag.h>
static Local  entries[2][LOCALS];
static Rtable tables[2];
#define REQUIRE(c)                                                                                 \
  do {                                                                                             \
    if (!(c))                                                                                      \
      panic("local authorization");                                                                \
  } while (0)
void
localtest(void)
{
  Setid    group;
  Acctid   acct;
  Spaceid  a, b;
  Handle   aspace, aalloc, bspace, balloc, data, read, code, execute, out;
  Allocarg args;
  Acct     account;
  Pgstat   before, after;
  u32      i;
  const u8 bytes[4] = {1, 2, 3, 4};
  pgstat(&before);
  REQUIRE(handletype(1) == HANDLE_PROCESS && !handletype(-1) && !handletype(0));
  REQUIRE(!handlepack(HANDLE_RESOURCE, HANDLE_GENERATION_MAX + 1, 0));
  out = handlepack(HANDLE_RESOURCE, HANDLE_GENERATION_MAX, 65535);
  REQUIRE(out > 0 && handlegen(out) == HANDLE_GENERATION_MAX && handleindex(out) == 65535);
  REQUIRE(!setcreate(4096, 8192, 8, &group) && !acctcreate(group, 4096, 8192, &acct));
  REQUIRE(!spacecreate(acct, &a) && !spacecreate(acct, &b));
  REQUIRE(!localinit(&tables[0], entries[0], LOCALS, acct, a) &&
          !localinit(&tables[1], entries[1], LOCALS, acct, b));
  REQUIRE(!localspace(&tables[0], 3, &aspace) && !localallocator(&tables[0], 5, &aalloc));
  REQUIRE(!localspace(&tables[1], 3, &bspace) && !localallocator(&tables[1], 5, &balloc));
  args.bytes = 8192;
  args.kind = ALLOC_DATA;
  args.align = args.minaddr = args.maxaddr = args.reserved = 0;
  REQUIRE(localalloc(&tables[0], 1, &args, &out) == -ERR_BAD_HANDLE && !out);
  REQUIRE(!localalloc(&tables[0], aalloc, &args, &data));
  REQUIRE(
      !localcopy(&tables[0], data, &tables[1], VM_READ, PAGE_SIZE, PAGE_SIZE, LOCAL_FORK, &read));
  REQUIRE(localmap(&tables[1], bspace, read, 0, 0x10000, PAGE_SIZE, VM_READ) == -ERR_DENIED);
  REQUIRE(localmap(&tables[1], bspace, read, PAGE_SIZE, 0x10000, PAGE_SIZE, VM_READ | VM_WRITE) ==
          -ERR_DENIED);
  REQUIRE(!localmap(&tables[1], bspace, read, PAGE_SIZE, 0x10000, PAGE_SIZE, VM_READ));
  REQUIRE(!localfree(&tables[0], data) && !localfree(&tables[1], read));
  REQUIRE(!localunmap(&tables[1], bspace, 0x10000));
  args.bytes = PAGE_SIZE;
  args.kind = ALLOC_CODE;
  REQUIRE(!localalloc(&tables[0], aalloc, &args, &code));
  REQUIRE(!localwrite(&tables[0], code, 0, bytes, sizeof(bytes)));
  REQUIRE(!localcopy(&tables[0], code, &tables[1], VM_READ | VM_EXEC, 0, PAGE_SIZE, LOCAL_FORK,
                     &execute));
  REQUIRE(localmap(&tables[1], bspace, execute, 0, 0x10000, PAGE_SIZE, VM_READ | VM_EXEC) ==
          -ERR_DENIED);
  REQUIRE(!localmap(&tables[0], aspace, code, 0, 0x10000, PAGE_SIZE, VM_READ | VM_WRITE));
  REQUIRE(localseal(&tables[0], code) == -ERR_BUSY);
  REQUIRE(!localunmap(&tables[0], aspace, 0x10000) && !localseal(&tables[0], code));
  REQUIRE(localwrite(&tables[0], code, 0, bytes, sizeof(bytes)) == -ERR_DENIED);
  REQUIRE(localcopy(&tables[0], code, &tables[1], VM_READ | VM_WRITE, 0, PAGE_SIZE, 0, &out) ==
              -ERR_DENIED &&
          !out);
  REQUIRE(!localmap(&tables[1], bspace, execute, 0, 0x10000, PAGE_SIZE, VM_READ | VM_EXEC));
  for (i = 3; i < LOCALS; i++)
    REQUIRE(!localcopy(&tables[0], code, &tables[0], VM_READ, 0, PAGE_SIZE, 0, &out));
  REQUIRE(localalloc(&tables[0], aalloc, &args, &out) == -ERR_CAPACITY && !out);
  REQUIRE(localfree(&tables[0], aspace) == -ERR_BAD_HANDLE);
  REQUIRE(!localclose(&tables[0]));
  for (i = 0; i < 4; i++)
    REQUIRE(localdrain(&tables[0], 32) == (i == 3 ? 0 : 1));
  REQUIRE(localfree(&tables[0], code) == -ERR_BAD_HANDLE);
  REQUIRE(!localfree(&tables[1], execute) && !localunmap(&tables[1], bspace, 0x10000));
  REQUIRE(!localclose(&tables[1]));
  while (localdrain(&tables[1], 32) > 0) {
  }
  REQUIRE(!acctclose(acct) && !acctunbind(acct) && !spacedestroy(a) && !spacedestroy(b));
  REQUIRE(acctstat(acct, &account) == AC_BAD && !setclose(group));
  pgstat(&after);
  REQUIRE(after.free == before.free && after.allocated == before.allocated && !acctcheck() &&
          !pgcheck());
  putstr("LOCALCHECK slots=");
  puthex(LOCALS);
  putstr(" charged=");
  puthex(0);
  putstr(" allocated=");
  puthex(after.allocated);
  putstr("\n");
}
