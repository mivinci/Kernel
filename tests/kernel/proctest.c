#include "ktest.h"
#include <proc.h>
#include <abi/boot.h>
#include <hal/context.h>
#include <diag.h>
extern const u8 proc_begin[], proc_end[];
extern void     arch_proccheck(Hctx *context);
static Handle   pool[PROCS - 1];
static u32      runs;
#define REQUIRE(c)                                                                                 \
  do {                                                                                             \
    if (!(c))                                                                                      \
      panic("process construction");                                                               \
  } while (0)
static void
clean(Handle pid)
{
  u32 i;
  int error;
  for (i = 0; i < 32; i++) {
    error = procclean(pid);
    REQUIRE(error >= 0);
    if (!error)
      return;
  }
  panic("process cleanup bound");
}
static Handle
resource(Rtable *t, u32 subtype)
{
  u32 i;
  for (i = 0; i < t->count; i++)
    if (t->slots[i].subtype == subtype)
      return handlepack(HANDLE_RESOURCE, t->slots[i].generation, i);
  panic("missing process resource");
}
static Handle
load(Handle pid)
{
  Rtable  *t = proctable(pid);
  Handle   allocator, space, code, stack, boot;
  Allocarg args;
  Config   c, bad;
  Boot     header;
  u32      i;
  REQUIRE(t != NULL);
  allocator = resource(t, LOCAL_ALLOC);
  space = resource(t, LOCAL_SPACE);
  args.bytes = PAGE_SIZE;
  args.kind = ALLOC_CODE;
  args.align = args.minaddr = args.maxaddr = args.reserved = 0;
  REQUIRE(!localalloc(t, allocator, &args, &code));
  REQUIRE(!localwrite(t, code, 0, proc_begin, (u64) (proc_end - proc_begin)) &&
          !localseal(t, code));
  args.bytes = 65536;
  args.kind = ALLOC_DATA;
  REQUIRE(!localalloc(t, allocator, &args, &stack));
  args.bytes = PAGE_SIZE;
  REQUIRE(!localalloc(t, allocator, &args, &boot));
  for (i = 0; i < sizeof(header); i++)
    ((u8 *) &header)[i] = 0;
  header.magic = BOOT_MAGIC;
  header.format = 1;
  header.bytes = sizeof(header);
  header.user_lo = 0x10000;
  header.user_hi = 1UL << 38;
  REQUIRE(!localwrite(t, boot, 0, &header, sizeof(header)));
  REQUIRE(!localmap(t, space, code, 0, 0x10000, PAGE_SIZE, VM_READ | VM_EXEC));
  REQUIRE(!localmap(t, space, stack, 0, 0x3ffffe0000UL, 65536, VM_READ | VM_WRITE));
  REQUIRE(!localmap(t, space, boot, 0, 0x3fff000000UL, PAGE_SIZE, VM_READ));
  c.entry = 0x10000;
  c.stack = 0x3fffff0000UL;
  c.boot = 0x3fff000000UL;
  c.bootsz = sizeof(header);
  c.sigmask = c.sigdefault = 0;
  REQUIRE(!procconfigure(pid, &c, 0));
  bad.entry = c.entry;
  bad.stack = c.stack + 1;
  bad.boot = c.boot;
  bad.bootsz = c.bootsz;
  bad.sigmask = bad.sigdefault = 0;
  REQUIRE(procconfigure(pid, &bad, 0) == -ERR_INVALID);
  REQUIRE(procconfigure(pid, &c, 1) == -ERR_NOTSUP);
  REQUIRE(!localunmap(t, space, 0x10000));
  REQUIRE(procpublish(pid) == -ERR_FAULT);
  REQUIRE(!localmap(t, space, code, 0, 0x10000, PAGE_SIZE, VM_READ | VM_EXEC));
  header.bytes++;
  REQUIRE(!localwrite(t, boot, 0, &header, sizeof(header)));
  REQUIRE(procpublish(pid) == -ERR_INVALID);
  header.bytes--;
  REQUIRE(!localwrite(t, boot, 0, &header, sizeof(header)));
  return code;
}
NORETURN void
proctesttrap(Hctx *context)
{
  Htrap trap;
  hal_trap_decode(context, &trap);
  REQUIRE(trap.fromuser && trap.kind == HT_SYSCALL && trap.syscall_nr == 1);
  arch_proccheck(context);
  runs++;
  procexit(LIFE_EXIT, 42, NULL);
}
void
proctest(void)
{
  Setid      set;
  Set        group;
  Procinfo   info;
  Lifeid     first, second;
  Life       retained;
  Liferesult result;
  Handle     a, b, c, oldcode, allocator, freshmem;
  Rtable    *t;
  Allocarg   args;
  Pgstat     before, after;
  u32        i;
  pgstat(&before);
  REQUIRE(!setcreate(4096, 8192, PROCS, &set));
  for (i = 1; i < 6; i++) {
    a = 77;
    REQUIRE(procnew(set, 1, i, i, LOCALS, &a) == -ERR_LIMIT && !a);
    REQUIRE(!setstat(set, &group) && !group.charged && !group.instances);
  }
  REQUIRE(procnew(set, 1, 128, 256, 1, &a) == -ERR_CAPACITY && !a);
  for (i = 0; i < PROCS - 1; i++)
    REQUIRE(!procnew(set, 1, 128, 256, LOCALS, &pool[i]));
  REQUIRE(procnew(set, 1, 128, 256, LOCALS, &a) == -ERR_CAPACITY && !a);
  REQUIRE(!setstat(set, &group) && group.instances == PROCS - 1 &&
          group.charged == 6 * (PROCS - 1));
  for (i = 0; i < PROCS - 1; i++) {
    REQUIRE(!procdestroy(pool[i]));
    clean(pool[i]);
  }
  REQUIRE(!procnew(set, 1, 128, 256, LOCALS, &a) && !procnew(set, 1, 128, 256, LOCALS, &b));
  REQUIRE(procpublish(a) == -ERR_STATE);
  oldcode = load(a);
  load(b);
  REQUIRE(!procinfo(a, &info));
  first = info.life;
  REQUIRE(!procinfo(b, &info));
  second = info.life;
  REQUIRE(!procpublish(a) && !procpublish(b));
  REQUIRE(!proctable(a) && procpublish(a) == -ERR_STATE && procdestroy(a) == -ERR_STATE);
  REQUIRE(!procrunone() && runs == 1);
  clean(a);
  REQUIRE(procinfo(a, &info) == -ERR_STALE && !lifestat(first, &retained) && retained.pid == a);
  REQUIRE(!procnew(set, 1, 128, 256, LOCALS, &c) && handleindex(c) == handleindex(a) && c != a);
  t = proctable(c);
  allocator = resource(t, LOCAL_ALLOC);
  args.bytes = PAGE_SIZE;
  args.kind = ALLOC_DATA;
  args.align = args.minaddr = args.maxaddr = args.reserved = 0;
  REQUIRE(!localalloc(t, allocator, &args, &freshmem) && freshmem != oldcode);
  REQUIRE(localfree(t, oldcode) == -ERR_BAD_HANDLE);
  REQUIRE(!procdestroy(c));
  clean(c);
  REQUIRE(!lifecollect(first, 1, &result) && result.pid == a && result.code == 42);
  REQUIRE(!procrunone() && runs == 2);
  clean(b);
  REQUIRE(!lifecollect(second, 1, &result) && result.pid == b && result.code == 42);
  REQUIRE(procrunone() == -ERR_WOULD_BLOCK);
  REQUIRE(!setstat(set, &group) && !group.charged && !group.instances && !setclose(set));
  pgstat(&after);
  REQUIRE(after.allocated == before.allocated && after.free == before.free && !acctcheck() &&
          !pgcheck());
  putstr("PROCCHECK constructed=");
  puthex(PROCS - 1);
  putstr(" runs=");
  puthex(runs);
  putstr(" exit=");
  puthex(42);
  putstr(" reused=");
  puthex(1);
  putstr(" charged=");
  puthex(group.charged);
  putstr(" allocated=");
  puthex(after.allocated);
  putstr("\n");
}
