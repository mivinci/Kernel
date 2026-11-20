#include "ktest.h"
#include "load.h"
#include <abi/boot.h>
#include <diag.h>
#include <signal.h>
#include <prep.h>

typedef struct Faultpacket Faultpacket;
struct Faultpacket
{
  Boot     header;
  Strref   argument;
  Resentry resources[2];
  char     mode[2];
};
extern const u8  fault_image[], fault_image_end[];
extern const u64 fault_trace_address;
#define CHECK(c)                                                                                   \
  do {                                                                                             \
    if (!(c)) {                                                                                    \
      putstr("FAULTFAIL line=");                                                                   \
      puthex(__LINE__);                                                                            \
      putstr("\n");                                                                                \
      panic("hardware signal");                                                                    \
    }                                                                                              \
  } while (0)

void
faulttest(void)
{
  Faultpacket packet;
  Procinfo    process;
  Setid       set;
  Set         account;
  Handle      pid;
  Rtable     *table;
  Life        life;
  Liferesult  result;
  Pgstat      before, after;
  Sigstat     signal;
  u64         trace[16];
  u32         mode, i, turns;
  int         error;
  pgstat(&before);
  CHECK(!setcreate(8192, 16384, 16, &set));
  for (mode = 0; mode < 11; mode++) {
    CHECK(!procnew(set, 1, 512, 1024, LOCALS, &pid) && !procinfo(pid, &process));
    table = proctable(pid);
    for (i = 0; i < sizeof(packet); i++)
      ((u8 *) &packet)[i] = 0;
    packet.header.magic = BOOT_MAGIC;
    packet.header.format = BOOT_FORMAT;
    packet.header.bytes = sizeof(packet);
    packet.header.argc = 1;
    packet.header.argv_off = offsetof(Faultpacket, argument);
    packet.header.resource_count = 2;
    packet.header.resources_off = offsetof(Faultpacket, resources);
    packet.header.user_lo = BOOT_USER_LO;
    packet.header.user_hi = BOOT_USER_HI;
    packet.argument.offset = offsetof(Faultpacket, mode);
    packet.argument.length = 2;
    packet.mode[0] = (char) ('0' + mode);
    packet.resources[0].tag = BOOT_SELF_SPACE;
    packet.resources[0].handle = testresource(table, LOCAL_SPACE);
    packet.resources[1].tag = BOOT_SELF_ALLOC;
    packet.resources[1].handle = testresource(table, LOCAL_ALLOC);
    testload(pid, fault_image, (u64) (fault_image_end - fault_image), &packet, sizeof(packet));
    CHECK(!procpublish(pid));
    for (turns = 0; turns < 1024; turns++) {
      CHECK(!procinfo(pid, &process));
      if (process.state == PROC_EXITING)
        break;
      error = procrunone();
      CHECK(!error || error == -ERR_WOULD_BLOCK);
    }
    CHECK(turns < 1024 && !spaceread(process.space, fault_trace_address, trace, sizeof(trace)) &&
          !lifestat(process.life, &life));
    if (trace[12]) {
      putstr("FAULTUSER mode=");
      puthex(mode);
      putstr(" line=");
      puthex(trace[12]);
      putstr(" handlers=");
      puthex(trace[2]);
      putstr(" address=");
      puthex(trace[13]);
      putstr(" expected=");
      puthex(trace[4]);
      putstr(" reason=");
      puthex(trace[14]);
      putstr(" signal=");
      puthex(trace[15]);
      putstr("\n");
    }
    CHECK(!trace[12] && trace[1] == mode);
    if (mode < 5)
      CHECK(life.reason == LIFE_EXIT && life.code == 42 && trace[0] == 2 && trace[2] == 1 &&
            trace[10] == 1 && trace[11] == 1 && trace[9] == trace[3]);
    else {
      CHECK(life.reason == LIFE_FAULT && life.code == SIGNAL_SEGV && trace[0] == 1 && !trace[11] &&
            life.fault.pc == trace[3] && life.fault.address == trace[4] && life.fault.cause &&
            life.fault.cause != SIGNAL_BAD_FRAME_CAUSE);
      CHECK(trace[2] == (mode == 10 ? 2U : mode == 9 ? 1U : 0U));
    }
    putstr("HARDFAULT mode=");
    puthex(mode);
    putstr(" handlers=");
    puthex(trace[2]);
    putstr(" reason=");
    puthex(life.reason);
    putstr(" code=");
    puthex(life.code);
    putstr(" cause=");
    puthex(mode < 5 ? trace[7] : life.fault.cause);
    putstr("\n");
    CHECK(!sigstat(process.identity, &signal) && !signal.depth && !sigcheck());
    while ((error = procclean(pid)) > 0) {
    }
    CHECK(!error && !lifecollect(process.life, 1, &result));
    for (i = 0; i < 128; i++)
      CHECK(procwork(32) >= 0 && prepwork(32) >= 0);
    CHECK(!setstat(set, &account) && !account.charged && !account.instances);
  }
  CHECK(!setclose(set));
  pgstat(&after);
  CHECK(before.allocated == after.allocated && before.free == after.free && !pgcheck() &&
        !acctcheck() && !sigcheck() && !lifecheck() && !groupcheck());
  putstr("HARDFAULTCHECK cases=");
  puthex(11);
  putstr(" recovered=");
  puthex(5);
  putstr(" fatal=");
  puthex(6);
  putstr(" charged=");
  puthex(0);
  putstr(" allocated=");
  puthex(after.allocated);
  putstr("\n");
}
