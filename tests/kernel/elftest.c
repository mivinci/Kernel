#include "ktest.h"
#include "load.h"
#include <abi/boot.h>
#include <prep.h>
#include <diag.h>
extern const u8  elf_image[], elf_image_end[];
extern const u64 elf_trace_address;
#define REQUIRE(c)                                                                                 \
  do {                                                                                             \
    if (!(c)) {                                                                                    \
      putstr("ELFFAIL line=");                                                                     \
      puthex(__LINE__);                                                                            \
      putstr("\n");                                                                                \
      panic("native user ELF loader");                                                             \
    }                                                                                              \
  } while (0)
void
elftest(void)
{
  Setid        set;
  Set          account;
  Handle       pid;
  Procinfo     parent, children[6];
  Createpolicy policy;
  Rtable      *table;
  Handle       create;
  struct
  {
    Boot     header;
    Resentry resources[3];
  } boot;
  Pgstat     before, after;
  Life       life;
  Liferesult result;
  u64        trace[16];
  u32        i, have[6], turns = 0;
  int        error;
  pgstat(&before);
  REQUIRE(!setcreate(8192, 16384, 16, &set));
  REQUIRE(!procnew(set, 1, 512, 1024, LOCALS, &pid) && !procinfo(pid, &parent));
  table = proctable(pid);
  policy.set = set;
  policy.memlimit = 512;
  policy.exec_limit = 1024;
  policy.resources = LOCALS;
  policy.pending = 16;
  policy.rights = CREATE_USE;
  REQUIRE(!localcreate(table, &policy, &create));
  for (i = 0; i < sizeof(boot); i++)
    ((u8 *) &boot)[i] = 0;
  boot.header.magic = BOOT_MAGIC;
  boot.header.format = BOOT_FORMAT;
  boot.header.bytes = sizeof(boot);
  boot.header.user_lo = BOOT_USER_LO;
  boot.header.user_hi = BOOT_USER_HI;
  boot.header.resource_count = 3;
  boot.header.resources_off = sizeof(Boot);
  boot.resources[0].tag = BOOT_SELF_SPACE;
  boot.resources[0].handle = testresource(table, LOCAL_SPACE);
  boot.resources[1].tag = BOOT_SELF_ALLOC;
  boot.resources[1].handle = testresource(table, LOCAL_ALLOC);
  boot.resources[2].tag = BOOT_CREATE;
  boot.resources[2].handle = create;
  testload(pid, elf_image, (u64) (elf_image_end - elf_image), &boot, sizeof(boot));
  for (i = 0; i < 6; i++)
    have[i] = 0;
  REQUIRE(!procpublish(pid));
  for (;;) {
    REQUIRE(!procrunone() && ++turns < 256);
    REQUIRE(!spaceread(parent.space, elf_trace_address, trace, sizeof(trace)));
    for (i = 0; i < 6; i++)
      if (trace[8 + i] && !have[i] && !procinfo((Handle) trace[8 + i], &children[i]))
        have[i] = 1;
    REQUIRE(!lifestat(parent.life, &life));
    if (life.state == LIFE_ENDED) {
      if (life.reason != LIFE_EXIT || life.code != 42) {
        putstr("ELFRESULT stage=");
        puthex(trace[0]);
        putstr(" reason=");
        puthex(life.reason);
        putstr(" code=");
        puthex(life.code);
        putstr("\n");
        panic("user loader result");
      }
      break;
    }
    REQUIRE(procwork(8) >= 0 && prepwork(8) >= 0);
  }
  REQUIRE(trace[0] == 99 && trace[1] == 2 && trace[2] == 4 && trace[3] == 1 && trace[4] > 1 &&
          trace[5] == 6 && trace[6] == 6 && trace[7] == 2 && trace[14] == 6);
  while ((error = procclean(pid)) > 0) {
  }
  REQUIRE(!error && !lifecollect(parent.life, 1, &result) && result.code == 42);
  for (i = 0; i < 256; i++)
    REQUIRE(procwork(32) >= 0 && prepwork(32) >= 0);
  for (i = 0; i < 6; i++)
    REQUIRE(have[i] && lifestat(children[i].life, &life) == -ERR_STALE);
  REQUIRE(!setstat(set, &account) && !account.charged && !account.instances && !setclose(set));
  pgstat(&after);
  REQUIRE(before.allocated == after.allocated && before.free == after.free && !pgcheck() &&
          !acctcheck());
  putstr("ELFCHECK turns=");
  puthex(turns);
  putstr(" children=");
  puthex(trace[1]);
  putstr(" boot_args=");
  puthex(trace[14]);
  putstr(" rollback=");
  puthex(trace[2]);
  putstr(" invalid=");
  puthex(trace[3]);
  putstr(" pressure=");
  puthex(trace[4]);
  putstr(" slots=");
  puthex(3);
  putstr(" charged=");
  puthex(account.charged);
  putstr(" allocated=");
  puthex(after.allocated);
  putstr("\n");
}
