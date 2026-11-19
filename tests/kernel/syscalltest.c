#include "ktest.h"
#include <proc.h>
#include <abi/boot.h>
#include <diag.h>
#include <clock.h>
#include <hal/clock.h>
extern const u8  native_begin[], native_end[], native_fault[];
extern const u8  native_illegal[], native_align[];
extern const u64 native_align_signal, native_align_pc_offset;
#define REQUIRE(c)                                                                                 \
  do {                                                                                             \
    if (!(c)) {                                                                                    \
      putstr("SYSCALLFAIL line=");                                                                 \
      puthex(__LINE__);                                                                            \
      putstr("\n");                                                                                \
      panic("native syscall");                                                                     \
    }                                                                                              \
  } while (0)
static Handle
resource(Rtable *t, u32 kind)
{
  u32 i;
  for (i = 0; i < t->count; i++)
    if (t->slots[i].subtype == kind)
      return handlepack(HANDLE_RESOURCE, t->slots[i].generation, i);
  panic("native boot resource");
}
static void
load(Handle pid, u64 entry, u32 fault)
{
  Rtable *t = proctable(pid);
  Handle  allocator = resource(t, LOCAL_ALLOC), space = resource(t, LOCAL_SPACE), code, stack, boot;
  Allocarg args;
  Config   c;
  Boot     header;
  u64      resources[8];
  u32      i;
  args.bytes = PAGE_SIZE;
  args.kind = ALLOC_CODE;
  args.align = args.minaddr = args.maxaddr = args.reserved = 0;
  REQUIRE(!localalloc(t, allocator, &args, &code));
  REQUIRE((u64) (native_end - native_begin) <= PAGE_SIZE);
  REQUIRE(!localwrite(t, code, 0, native_begin, (u64) (native_end - native_begin)) &&
          !localseal(t, code));
  args.bytes = 65536;
  args.kind = ALLOC_DATA;
  REQUIRE(!localalloc(t, allocator, &args, &stack));
  args.bytes = PAGE_SIZE;
  REQUIRE(!localalloc(t, allocator, &args, &boot));
  for (i = 0; i < sizeof(header); i++)
    ((u8 *) &header)[i] = 0;
  header.magic = BOOT_MAGIC;
  header.format = BOOT_FORMAT;
  header.bytes = sizeof(header) + sizeof(resources);
  header.resource_count = 2;
  header.resources_off = sizeof(header);
  header.user_lo = 0x10000;
  header.user_hi = 1UL << 38;
  for (i = 0; i < 8; i++)
    resources[i] = 0;
  resources[0] = 1;
  resources[1] = (u64) space;
  resources[4] = 2;
  resources[5] = (u64) allocator;
  REQUIRE(!localwrite(t, boot, 0, &header, sizeof(header)) &&
          !localwrite(t, boot, sizeof(header), resources, sizeof(resources)));
  REQUIRE(!localmap(t, space, code, 0, entry, PAGE_SIZE, VM_READ | VM_EXEC));
  REQUIRE(!localmap(t, space, stack, 0, 0x3ffffe0000UL, 65536, VM_READ | VM_WRITE));
  REQUIRE(!localmap(t, space, boot, 0, 0x3fff000000UL, PAGE_SIZE, VM_READ));
  c.entry = entry + (fault == 1   ? (u64) (native_fault - native_begin)
                     : fault == 2 ? (u64) (native_illegal - native_begin)
                     : fault == 3 ? (u64) (native_align - native_begin)
                                  : 0);
  c.stack = 0x3fffff0000UL;
  c.boot = 0x3fff000000UL;
  c.bootsz = header.bytes;
  c.sigmask = c.sigdefault = 0;
  REQUIRE(!procconfigure(pid, &c, 0) && !procpublish(pid));
}
void
syscalltest(void)
{
  Setid      set;
  Set        group;
  Handle     pids[5];
  Lifeid     ids[5];
  Procinfo   info;
  Life       life;
  Liferesult result;
  Pgstat     before, after;
  u32        i, j, turns = 0, expected, reason;
  int        error;
  u64        start, now, frequency = hal_clock_freq(), nanos;
  isize      seconds;
  REQUIRE(frequency && !clockmono(&start));
  now = start;
  for (i = 0; i < 100000 && now == start; i++)
    REQUIRE(!clockmono(&now));
  REQUIRE(now > start);
  REQUIRE(!clockres(1, &seconds, &nanos));
  REQUIRE((u64) seconds * 1000000000UL + nanos ==
          1000000000UL / frequency + (1000000000UL % frequency != 0));
  putstr("CLOCK frequency=");
  puthex(frequency);
  putstr(" resolution_ns=");
  puthex((u64) seconds * 1000000000UL + nanos);
  putstr("\n");
  pgstat(&before);
  REQUIRE(!setcreate(4096, 8192, 8, &set));
  for (i = 0; i < 5; i++) {
    REQUIRE(!procnew(set, 1, 256, 512, LOCALS, &pids[i]) && !procinfo(pids[i], &info));
    ids[i] = info.life;
    load(pids[i], 0x10000 + (u64) i * 0x10000, i < 2 ? 0 : i - 1);
  }
  while (!(error = procrunone())) {
    REQUIRE(++turns <= 10);
  }
  REQUIRE(error == -ERR_WOULD_BLOCK && turns == 8);
  for (i = 0; i < 5; i++) {
    expected = i < 2 ? 42 : i == 2 ? SIGNAL_SEGV : i == 3 ? SIGNAL_ILL : (u32) native_align_signal;
    reason = i < 2 ? LIFE_EXIT : LIFE_FAULT;
    REQUIRE(!lifestat(ids[i], &life));
    if (life.reason != reason || life.code != expected) {
      putstr("SYSCALLRESULT code=");
      puthex(life.code);
      putstr(" pc=");
      puthex(life.fault.pc);
      putstr("\n");
    }
    REQUIRE(life.reason == reason && life.code == expected);
    if (i == 2)
      REQUIRE(life.fault.address == 0x40000);
    if (i == 3)
      REQUIRE(life.fault.pc == 0x10000 + (u64) i * 0x10000 + (u64) (native_illegal - native_begin));
    if (i == 4)
      REQUIRE(life.fault.pc == 0x10000 + (u64) i * 0x10000 + native_align_pc_offset);
    for (j = 0; j < 32; j++) {
      error = procclean(pids[i]);
      REQUIRE(error >= 0);
      if (!error)
        break;
    }
    REQUIRE(j < 32 && !lifecollect(ids[i], 1, &result) && result.code == expected &&
            result.reason == reason);
  }
  REQUIRE(!setstat(set, &group) && !group.charged && !group.instances && !setclose(set));
  pgstat(&after);
  REQUIRE(before.free == after.free && before.allocated == after.allocated && !acctcheck() &&
          !pgcheck());
  putstr("SYSCALLCHECK processes=");
  puthex(5);
  putstr(" turns=");
  puthex(turns);
  putstr(" exit=");
  puthex(42);
  putstr(" faults=");
  puthex(3);
  putstr(" illegal=");
  puthex(native_align_signal == SIGNAL_BUS ? 1 : 2);
  putstr(" pc_align=");
  puthex(native_align_signal == SIGNAL_BUS ? 1 : 0);
  putstr(" charged=");
  puthex(group.charged);
  putstr(" allocated=");
  puthex(after.allocated);
  putstr("\n");
}
