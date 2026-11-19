#include "ktest.h"
#include "load.h"
#include <diag.h>
#include <abi/boot.h>
#include <clock.h>
extern const u8  tty_image[], tty_image_end[];
extern const u64 tty_trace_address;
#define CHECK(c)                                                                                   \
  do {                                                                                             \
    if (!(c)) {                                                                                    \
      putstr("TTYFAIL line=");                                                                     \
      puthex(__LINE__);                                                                            \
      putstr("\n");                                                                                \
      panic("tty userspace state");                                                                \
    }                                                                                              \
  } while (0)

void
ttytest(void)
{
  struct
  {
    Boot     header;
    Resentry resources[2];
  } packet;
  Setid      set;
  Set        account;
  Handle     pid;
  Procinfo   process;
  Life       life;
  Liferesult result;
  Pgstat     before, after;
  u64        trace[8], started, current;
  u32        i, blocked = 0;
  int        error;
  pgstat(&before);
  CHECK(!setcreate(1024, 2048, 1, &set) && !procnew(set, 1, 128, 256, LOCALS, &pid) &&
        !procinfo(pid, &process));
  for (i = 0; i < sizeof(packet); i++)
    ((u8 *) &packet)[i] = 0;
  packet.header.magic = BOOT_MAGIC;
  packet.header.format = BOOT_FORMAT;
  packet.header.bytes = sizeof(packet);
  packet.header.user_lo = BOOT_USER_LO;
  packet.header.user_hi = BOOT_USER_HI;
  packet.header.resource_count = 2;
  packet.header.resources_off = sizeof(Boot);
  packet.resources[0].tag = BOOT_SELF_SPACE;
  packet.resources[0].handle = testresource(proctable(pid), LOCAL_SPACE);
  packet.resources[1].tag = BOOT_SELF_ALLOC;
  packet.resources[1].handle = testresource(proctable(pid), LOCAL_ALLOC);
  testload(pid, tty_image, (u64) (tty_image_end - tty_image), &packet, sizeof(packet));
  CHECK(!procpublish(pid) && !proctimerstart() && !clockmono(&started));
  for (;;) {
    error = procrunone();
    CHECK(!error || error == -ERR_WOULD_BLOCK);
    CHECK(!lifestat(process.life, &life) && !clockmono(&current) &&
          current - started < UINT64_C(3000000000));
    if (life.state == LIFE_ENDED)
      break;
    CHECK(!procinfo(pid, &process));
    if (process.state == PROC_BLOCKED)
      blocked = 1;
    if (error == -ERR_WOULD_BLOCK)
      CHECK(procidle() >= 0);
  }
  CHECK(life.reason == LIFE_EXIT && life.code == 42 && blocked &&
        !spaceread(process.space, tty_trace_address, trace, sizeof(trace)) && trace[0] == 1 &&
        trace[1] == 2 && trace[3] >= trace[2] && trace[4] == 1 && trace[5] == 1 && trace[6] == 1 &&
        trace[7] == 42);
  while ((error = procclean(pid)) > 0) {
  }
  CHECK(!error && !lifecollect(process.life, 1, &result) && result.code == 42 &&
        !setstat(set, &account) && !account.charged && !account.instances && !setclose(set));
  pgstat(&after);
  CHECK(before.free == after.free && before.allocated == after.allocated && !pgcheck() &&
        !acctcheck());
  putstr("TTYCHECK input=");
  puthex(trace[0]);
  putstr(" alarm=");
  puthex(trace[1]);
  putstr(" interbyte=");
  puthex(trace[4]);
  putstr(" nonblock=");
  puthex(trace[5]);
  putstr(" canonical=");
  puthex(trace[6]);
  putstr(" blocked=");
  puthex(blocked);
  putstr(" charged=");
  puthex(account.charged);
  putstr(" allocated=");
  puthex(after.allocated);
  putstr("\n");
}
