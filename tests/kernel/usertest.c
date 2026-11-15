#include "ktest.h"
#include <space.h>
#include <diag.h>
#include <hal/boot.h>
#include <vm.h>
extern const u8 user_begin[], user_end[];
static Spaceid  userspace;
static Memid    code, stack, boot;
static Setid    group;
static Acctid   acct;
static Pgstat   baseline;
static u32      phase;
#define REQUIRE(c)                                                                                 \
  do {                                                                                             \
    if (!(c))                                                                                      \
      panic("user context");                                                                       \
  } while (0)
NORETURN void
usertest(Hvs *kernel)
{
  Hctx  context;
  Hinit initial;
  Hpte  pte;
  u64   header[16];
  u32   i;
  UNUSED(kernel);
  pgstat(&baseline);
  REQUIRE(!setcreate(128, 256, 2, &group) && !acctcreate(group, 128, 256, &acct));
  REQUIRE(!spacecreate(acct, &userspace));
  REQUIRE(!memcode(acct, PAGE_SIZE, &code));
  REQUIRE(!memwrite(code, 0, user_begin, (u64) (user_end - user_begin)) && !memseal(code));
  REQUIRE(!memcreate(acct, 16 * PAGE_SIZE, &stack) && !memcreate(acct, PAGE_SIZE, &boot));
  for (i = 0; i < 16; i++)
    header[i] = 0;
  header[0] = 0x4b424f4f54303031UL;
  header[1] = 1;
  header[2] = sizeof(header);
  header[14] = 0x10000;
  header[15] = 1UL << 38;
  REQUIRE(!memwrite(boot, 0, header, sizeof(header)));
  REQUIRE(!spacemap(userspace, code, 0, 0x10000, PAGE_SIZE, VM_READ | VM_EXEC));
  REQUIRE(!spacemap(userspace, stack, 0, 0x3ffffe0000UL, 16 * PAGE_SIZE, VM_READ | VM_WRITE));
  REQUIRE(!spacemap(userspace, boot, 0, 0x3fff000000UL, PAGE_SIZE, VM_READ));
  REQUIRE(!spacelookup(userspace, 0x3ffffdf000UL, &pte));
  initial.entry = 0x10000;
  initial.sp = 0x3fffff0000UL;
  initial.arg = 0x3fff000000UL;
  for (i = 0; i < sizeof(context); i++)
    ((u8 *) &context)[i] = 0xff;
  hal_user_init(&context, &initial);
  REQUIRE(!spaceactivate(userspace));
  REQUIRE(spaceunmap(userspace, 0x10000) == SP_BUSY && spacedestroy(userspace) == SP_BUSY);
  hal_user_enter(&context);
}
NORETURN void
usertesttrap(Hctx *context)
{
  Htrap  trap;
  Pgstat after;
  hal_trap_decode(context, &trap);
  REQUIRE(trap.fromuser && trap.kind == (phase < 2 ? HT_SYSCALL : HT_PAGEFAULT));
  arch_usercheck(context, phase);
  if (phase >= 2)
    REQUIRE(trap.address == (phase == 2   ? 0x3fff000000UL
                             : phase == 3 ? 0x10000UL
                             : phase == 4 ? KBASE
                             : phase == 5 ? 0x3ffffdf000UL
                                          : 0x3ffffefff0UL));
  putstr("USERCHECK phase=");
  puthex(phase);
  putstr(" kind=");
  puthex(trap.kind);
  putstr(" address=");
  puthex(trap.address);
  putstr("\n");
  if (++phase < 7)
    hal_user_enter(context);
  REQUIRE(!spacedeactivate());
  REQUIRE(!spaceunmap(userspace, 0x10000) && !spaceunmap(userspace, 0x3ffffe0000UL));
  REQUIRE(!spaceunmap(userspace, 0x3fff000000UL));
  REQUIRE(!memfree(code) && !memfree(stack) && !memfree(boot));
  REQUIRE(!spacedestroy(userspace) && !acctclose(acct) && !acctunbind(acct) && !setclose(group));
  pgstat(&after);
  REQUIRE(after.free == baseline.free && after.allocated == baseline.allocated && !pgcheck() &&
          !acctcheck());
  putstr("READY user-restored\n");
  hal_halt();
}
