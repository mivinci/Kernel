#include "ktest.h"
#include <proc.h>
#include <signal.h>
#include <abi/boot.h>
#include <diag.h>
extern const u8 proc_begin[], proc_end[];
#define REQUIRE(c)                                                                                 \
  do {                                                                                             \
    if (!(c)) {                                                                                    \
      putstr("SIGNALFAIL line=");                                                                  \
      puthex(__LINE__);                                                                            \
      putstr("\n");                                                                                \
      panic("signal reservation");                                                                 \
    }                                                                                              \
  } while (0)
static Handle
resource(Rtable *t, u32 kind)
{
  u32 i;
  for (i = 0; i < t->count; i++)
    if (t->slots[i].subtype == kind)
      return handlepack(HANDLE_RESOURCE, t->slots[i].generation, i);
  panic("signal reservation boot resource");
}
static void
load(Handle pid)
{
  Rtable  *t = proctable(pid);
  Handle   allocator = resource(t, LOCAL_ALLOC), space = resource(t, LOCAL_SPACE);
  Handle   code, stack, boot;
  Allocarg args;
  Config   config;
  Boot     header;
  u32      i;
  args.bytes = PAGE_SIZE;
  args.kind = ALLOC_CODE;
  args.align = args.minaddr = args.maxaddr = args.reserved = 0;
  REQUIRE(!localalloc(t, allocator, &args, &code));
  REQUIRE((u64) (proc_end - proc_begin) < PAGE_SIZE);
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
  config.entry = 0x10000;
  config.stack = 0x3fffff0000UL;
  config.boot = 0x3fff000000UL;
  config.bootsz = sizeof(header);
  config.sigmask = config.sigdefault = 0;
  REQUIRE(!procconfigure(pid, &config, 0));
}
static void
clean(Handle pid)
{
  int error;
  while ((error = procclean(pid)) > 0) {
  }
  REQUIRE(!error);
}
void
signaltest(void)
{
  Setid      set;
  Set        group, baseline;
  Pgstat     before, after, stable, current;
  Procinfo   info[3], step;
  Handle     pids[3], failed;
  Sigstat    signal;
  Image      image;
  Life       life;
  Liferesult result;
  u32        bytes, i;
  u64        cookie, prior, fresh, top;
  pgstat(&before);
  REQUIRE(!setcreate(512, 1024, 8, &set));
  REQUIRE(!procnew(set, 1, 64, 128, 8, &pids[0]) && !procinfo(pids[0], &info[0]));
  REQUIRE(!sigstat(info[0].identity, &signal) && signal.pid == pids[0] && signal.image == 1 &&
          signal.slots == 2 && !signal.depth && !signal.mask && !signal.pending &&
          signal.bytes <= 1024 && !(signal.bytes & 15));
  bytes = signal.bytes;
  REQUIRE(!imagestat(info[0].identity, &image) && image.cleanup == 1);
  REQUIRE(sigreserve(info[0].identity) == -ERR_STATE && !sigcheck());
  REQUIRE(!setstat(set, &baseline));
  pgstat(&stable);
  for (i = 0; i < 2; i++) {
    failed = 77;
    REQUIRE(procnew(set, 1, i ? 64 : 1, 128, i ? 1 : 8, &failed) ==
                (i ? -ERR_CAPACITY : -ERR_LIMIT) &&
            !failed);
    REQUIRE(!setstat(set, &group) && group.refs == baseline.refs &&
            group.instances == baseline.instances && group.charged == baseline.charged);
    pgstat(&current);
    REQUIRE(current.allocated == stable.allocated && current.free == stable.free && !sigcheck());
  }
  REQUIRE(!procnew(set, 1, 64, 128, 8, &pids[1]) && !procinfo(pids[1], &info[1]));
  REQUIRE(!sigstat(info[1].identity, &signal) && signal.pid == pids[1] && !signal.depth);
  REQUIRE(!imagehold(info[1].identity, IMAGE_OBSERVER));
  load(pids[1]);
  REQUIRE(!kstacktop(info[1].stack, &top));
  arch_sigcontext((Hctx *) (top - sizeof(Hctx)), 0);
  cookie = 77;
  REQUIRE(sigcookie(info[1].identity, &cookie) == -ERR_STATE && !cookie);
  REQUIRE(!procpublish(pids[1]));
  REQUIRE(!sigcookie(info[1].identity, &cookie) && cookie);
  prior = cookie;
  REQUIRE(!sigcookie(info[1].identity, &cookie) && cookie > prior);
  prior = cookie;
  REQUIRE(sigrelease(info[1].identity) == -ERR_STATE && !sigcheck());
  REQUIRE(!procrunone() && !lifestat(info[1].life, &life));
  if (life.reason != LIFE_EXIT || life.code != 42) {
    putstr("SIGCTXRESULT reason=");
    puthex(life.reason);
    putstr(" code=");
    puthex(life.code);
    putstr(" pc=");
    puthex(life.fault.pc);
    putstr("\n");
  }
  REQUIRE(life.reason == LIFE_EXIT && life.code == 42);
  REQUIRE(!sigstat(info[1].identity, &signal));
  for (;;) {
    REQUIRE(!procinfo(pids[1], &step));
    if (step.phase == 2)
      break;
    REQUIRE(procclean(pids[1]) == 1);
  }
  REQUIRE(!sigstat(info[1].identity, &signal));
  REQUIRE(procclean(pids[1]) == 1);
  REQUIRE(sigstat(info[1].identity, &signal) == -ERR_STALE);
  clean(pids[1]);
  REQUIRE(!lifecollect(info[1].life, 1, &result) && result.code == 42);
  REQUIRE(!imagestat(info[1].identity, &image) && !image.owner && !image.cleanup &&
          image.observers == 1);
  REQUIRE(!procnew(set, 1, 64, 128, 8, &pids[2]) && !procinfo(pids[2], &info[2]));
  REQUIRE(handleindex(pids[2]) == handleindex(pids[1]) && pids[2] != pids[1]);
  REQUIRE(!sigstat(info[2].identity, &signal) && signal.pid == pids[2] && signal.slots == 2 &&
          !signal.depth && !signal.mask && !signal.pending && signal.bytes == bytes);
  REQUIRE(sigstat(info[1].identity, &signal) == -ERR_STALE &&
          sigrelease(info[1].identity) == -ERR_STALE);
  REQUIRE(!imagestat(info[2].identity, &image) && image.cleanup == 1);
  REQUIRE(!imagedrop(info[1].identity, IMAGE_OBSERVER));
  REQUIRE(lifestat(info[1].life, &life) == -ERR_STALE);
  load(pids[2]);
  REQUIRE(!kstacktop(info[2].stack, &top));
  arch_sigcontext((Hctx *) (top - sizeof(Hctx)), 1);
  cookie = 77;
  REQUIRE(sigcookie(info[1].identity, &cookie) == -ERR_STALE && !cookie);
  REQUIRE(!procpublish(pids[2]));
  REQUIRE(!sigcookie(info[2].identity, &fresh) && fresh > prior);
  REQUIRE(!sigcookie(info[2].identity, &cookie) && cookie > fresh);
  REQUIRE(!procrunone());
  clean(pids[2]);
  REQUIRE(!lifecollect(info[2].life, 1, &result) && result.code == 42);
  REQUIRE(!procdestroy(pids[0]));
  clean(pids[0]);
  REQUIRE(sigstat(info[0].identity, &signal) == -ERR_STALE &&
          sigstat(info[2].identity, &signal) == -ERR_STALE && !sigcheck());
  REQUIRE(!setstat(set, &group) && !group.instances && !group.charged && !setclose(set));
  pgstat(&after);
  REQUIRE(before.allocated == after.allocated && before.free == after.free && !pgcheck() &&
          !acctcheck());
  putstr("SIGNALCHECK contexts=");
  puthex(2);
  putstr(" bytes=");
  puthex(bytes);
  putstr(" restore=");
  puthex(2);
  putstr(" rejected=");
  puthex(16);
  putstr(" cookies=");
  puthex(1);
  putstr(" rollback=");
  puthex(2);
  putstr(" reused=");
  puthex(1);
  putstr(" charged=");
  puthex(group.charged);
  putstr(" allocated=");
  puthex(after.allocated);
  putstr("\n");
}
