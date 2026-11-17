#include "ktest.h"
#include <proc.h>
#include <hal/context.h>
#include <abi/boot.h>
#include <diag.h>
void            arch_forkcontext(Hctx *out, u64 pid, u64 fixva);
extern const u8 fork_code_begin[], fork_code_end[];
#define REQUIRE(c)                                                                                 \
  do {                                                                                             \
    if (!(c)) {                                                                                    \
      putstr("FORKCTXFAIL line=");                                                                 \
      puthex(__LINE__);                                                                            \
      putstr("\n");                                                                                \
      panic("fork user return frame");                                                             \
    }                                                                                              \
  } while (0)
static Handle
resource(Rtable *table, u32 kind)
{
  u32 i;
  for (i = 0; i < table->count; i++)
    if (table->slots[i].subtype == kind)
      return handlepack(HANDLE_RESOURCE, table->slots[i].generation, i);
  panic("fork fixture boot resource");
}
void
forkcontexttest(void)
{
  Setid      set;
  Set        group;
  Handle     pid, allocator, space, code, stack, boot, fix;
  Procinfo   process;
  Rtable    *table;
  Life       life;
  Liferesult result;
  Pgstat     before, after;
  Allocarg   request;
  Config     config;
  Boot       header;
  u64        top, fixva, marker = 0x4711;
  u32        mode, i;
  int        error;
  pgstat(&before);
  REQUIRE(!setcreate(512, 1024, 4, &set));
  for (mode = 0; mode < 2; mode++) {
    REQUIRE(!procnew(set, 1, 64, 128, 8, &pid) && !procinfo(pid, &process));
    table = proctable(pid);
    allocator = resource(table, LOCAL_ALLOC);
    space = resource(table, LOCAL_SPACE);
    request.bytes = PAGE_SIZE;
    request.kind = ALLOC_CODE;
    request.align = request.minaddr = request.maxaddr = request.reserved = 0;
    REQUIRE(!localalloc(table, allocator, &request, &code) &&
            (u64) (fork_code_end - fork_code_begin) < PAGE_SIZE &&
            !localwrite(table, code, 0, fork_code_begin, (u64) (fork_code_end - fork_code_begin)) &&
            !localseal(table, code) &&
            !localmap(table, space, code, 0, 0x10000, PAGE_SIZE, VM_READ | VM_EXEC));
    request.bytes = 65536;
    request.kind = ALLOC_DATA;
    REQUIRE(!localalloc(table, allocator, &request, &stack) &&
            !localmap(table, space, stack, 0, 0x3ffffe0000UL, 65536, VM_READ | VM_WRITE));
    request.bytes = PAGE_SIZE;
    REQUIRE(!localalloc(table, allocator, &request, &boot));
    for (i = 0; i < sizeof(header); i++)
      ((u8 *) &header)[i] = 0;
    header.magic = BOOT_MAGIC;
    header.format = BOOT_FORMAT;
    header.bytes = sizeof(header);
    header.user_lo = BOOT_USER_LO;
    header.user_hi = BOOT_USER_HI;
    REQUIRE(!localwrite(table, boot, 0, &header, sizeof(header)) &&
            !localmap(table, space, boot, 0, 0x3fff000000UL, PAGE_SIZE, VM_READ));
    fixva = mode ? 0x50000 : 0;
    if (mode)
      REQUIRE(!localalloc(table, allocator, &request, &fix) &&
              !localwrite(table, fix, 0, &marker, 8) &&
              !localmap(table, space, fix, 0, fixva, PAGE_SIZE, VM_READ));
    config.entry = 0x10004;
    config.stack = 0x3fffff0000UL;
    config.boot = 0x3fff000000UL;
    config.bootsz = sizeof(header);
    config.sigmask = config.sigdefault = 0;
    REQUIRE(!procconfigure(pid, &config, 0) && !kstacktop(process.stack, &top));
    /* Register-only captured-forkprep fixture; no native fork success stub.
       The restored user program starts immediately after its raw SVC/ECALL. */
    arch_forkcontext((Hctx *) (top - sizeof(Hctx)), (u64) pid, fixva);
    REQUIRE(!procpublish(pid) && !procrunone() && !lifestat(process.life, &life));
    if (life.reason != LIFE_EXIT || life.code != 42) {
      putstr("FORKCTXRESULT reason=");
      puthex(life.reason);
      putstr(" code=");
      puthex(life.code);
      putstr("\n");
    }
    REQUIRE(life.state == LIFE_ENDED && life.reason == LIFE_EXIT && life.code == 42);
    while ((error = procclean(pid)) > 0) {
    }
    REQUIRE(!error && !lifecollect(process.life, 1, &result));
  }
  REQUIRE(!setstat(set, &group) && !group.charged && !group.instances && !setclose(set) &&
          !pgcheck() && !acctcheck() && !lifecheck() && !groupcheck());
  pgstat(&after);
  REQUIRE(before.free == after.free && before.allocated == after.allocated);
  putstr("FORKCTXCHECK cases=");
  puthex(2);
  putstr(" resumed=");
  puthex(2);
  putstr(" fix=");
  puthex(1);
  putstr(" charged=");
  puthex(0);
  putstr(" allocated=");
  puthex(after.allocated);
  putstr("\n");
}
