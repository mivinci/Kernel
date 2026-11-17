#include "ktest.h"
#include "load.h"
#include <abi/boot.h>
#include <signal.h>
#include <prep.h>
#include <diag.h>
typedef struct Suspendpacket Suspendpacket;
struct Suspendpacket
{
  Boot     header;
  Strref   arg;
  Resentry resources[3];
  char     name[8];
};
extern const u8  suspend_image[], suspend_image_end[];
extern const u64 suspend_trace_address;
#define BIT(n) ((u64) 1 << ((n) - 1))
#define REQUIRE(c)                                                                                 \
  do {                                                                                             \
    if (!(c)) {                                                                                    \
      putstr("SUSPENDFAIL line=");                                                                 \
      puthex(__LINE__);                                                                            \
      putstr("\n");                                                                                \
      panic("signal suspend");                                                                     \
    }                                                                                              \
  } while (0)
void
suspendtest(void)
{
  Rtable       *table;
  Setid         set;
  Set           account;
  Handle        pid, notify, control;
  Procinfo      process, current;
  Suspendpacket packet;
  Siginfo       info;
  Sigstat       signal;
  Life          life;
  Liferesult    result;
  Pgstat        before, after;
  u64           trace[32];
  u32           mode, i, turns, prepared, first, nested, full;
  int           error;
  pgstat(&before);
  REQUIRE(!setcreate(8192, 16384, 16, &set));
  for (mode = 0; mode < 9; mode++) {
    REQUIRE(!procnew(set, 1, 512, 1024, LOCALS, &pid) && !procinfo(pid, &process));
    table = proctable(pid);
    REQUIRE(!localendpoint(table, process.identity, LOCAL_NOTIFY, LOCAL_USE, (u64) 1 << 32, 0,
                           &notify));
    REQUIRE(!localcontrol(table, process.life, CONTROL_TERMINATE, &control));
    for (i = 0; i < sizeof(packet); i++)
      ((u8 *) &packet)[i] = 0;
    packet.header.magic = BOOT_MAGIC;
    packet.header.format = BOOT_FORMAT;
    packet.header.bytes = sizeof(packet);
    packet.header.argc = 1;
    packet.header.argv_off = offsetof(Suspendpacket, arg);
    packet.header.resource_count = 3;
    packet.header.resources_off = offsetof(Suspendpacket, resources);
    packet.header.user_lo = BOOT_USER_LO;
    packet.header.user_hi = BOOT_USER_HI;
    packet.arg.offset = offsetof(Suspendpacket, name);
    packet.arg.length = 2;
    packet.name[0] = (char) ('0' + mode);
    packet.resources[0].tag = BOOT_SELF_SPACE;
    packet.resources[0].handle = testresource(proctable(pid), LOCAL_SPACE);
    packet.resources[1].tag = BOOT_SELF_ALLOC;
    packet.resources[1].handle = testresource(proctable(pid), LOCAL_ALLOC);
    packet.resources[2].tag = 257;
    packet.resources[2].handle = notify;
    testload(pid, suspend_image, (u64) (suspend_image_end - suspend_image), &packet,
             sizeof(packet));
    REQUIRE(!procpublish(pid));
    prepared = first = nested = full = turns = 0;
    for (;;) {
      REQUIRE(!procrunone() && ++turns < 64);
      REQUIRE(!spaceread(process.space, suspend_trace_address, trace, sizeof(trace)) &&
              !lifestat(process.life, &life));
      if (trace[31]) {
        putstr("SUSPENDUSER mode=");
        puthex(mode);
        putstr(" code=");
        puthex(trace[31]);
        putstr("\n");
      }
      REQUIRE(!trace[31]);
      if (life.state == LIFE_ENDED)
        break;
      REQUIRE(!procinfo(pid, &current));
      for (i = 0; i < sizeof(info); i++)
        ((u8 *) &info)[i] = 0;
      if (trace[0] == 1 && !prepared) {
        if (!mode) {
          info.signum = SIGNAL_USR1;
          REQUIRE(!procsigqueue(pid, &info) && !sigstat(process.identity, &signal) &&
                  !signal.depth && signal.pending == BIT(SIGNAL_USR1));
        }
        prepared = 1;
      }
      if (trace[0] == 2 && !first) {
        REQUIRE(mode && current.state == PROC_BLOCKED && procrunone() == -ERR_WOULD_BLOCK);
        if (mode == 1) {
          info.signum = SIGNAL_WINCH;
          REQUIRE(!procsigqueue(pid, &info));
          info.signum = SIGNAL_USR2;
          REQUIRE(!procsigqueue(pid, &info) && procrunone() == -ERR_WOULD_BLOCK);
          REQUIRE(!sigstat(process.identity, &signal) && !signal.depth &&
                  signal.mask == BIT(SIGNAL_USR2) && signal.pending == BIT(SIGNAL_USR2));
        }
        if (mode == 5 || mode == 8) {
          if (mode == 8) {
            info.signum = SIGNAL_USR1;
            REQUIRE(!procsigqueue(pid, &info) && !procinfo(pid, &current) &&
                    current.state == PROC_READY && !sigstat(process.identity, &signal) &&
                    !signal.depth);
          }
          error = procterminate(table, control);
          REQUIRE(!error && !lifestat(process.life, &life));
          break;
        }
        info.signum = mode == 4 ? SIGNAL_TERM : SIGNAL_USR1;
        REQUIRE(!procsigqueue(pid, &info));
        first = 1;
      }
      if (trace[0] == 3 && !nested) {
        REQUIRE(current.state == PROC_BLOCKED && !sigstat(process.identity, &signal) &&
                signal.depth == 1);
        info.signum = SIGNAL_USR2;
        REQUIRE(!procsigqueue(pid, &info));
        nested = 1;
      }
      if (trace[0] == 4 && !full) {
        REQUIRE(mode == 6 && current.state == PROC_BLOCKED && !sigstat(process.identity, &signal) &&
                signal.depth == 2 && !signal.mask);
        info.signum = SIGNAL_USR1;
        REQUIRE(!procsigqueue(pid, &info) && procrunone() == -ERR_WOULD_BLOCK);
        info.signum = SIGNAL_TERM;
        REQUIRE(!procsigqueue(pid, &info));
        full = 1;
      }
      REQUIRE(procwork(8) >= 0 && prepwork(8) >= 0);
    }
    putstr("SUSPENDCASE mode=");
    puthex(mode);
    putstr(" reason=");
    puthex(life.reason);
    putstr(" code=");
    puthex(life.code);
    putstr("\n");
    if (mode == 4 || mode == 6)
      REQUIRE(life.reason == LIFE_SIGNAL && life.code == SIGNAL_TERM && !trace[5] &&
              (mode != 6 || (full && trace[4] == 2)));
    else if (mode == 5 || mode == 8)
      REQUIRE(life.reason == LIFE_SIGNAL && life.code == SIGNAL_KILL && !trace[5] && !trace[4]);
    else
      REQUIRE(life.reason == LIFE_EXIT && life.code == 42 && trace[0] == 99 && trace[5] == 1 &&
              (mode != 2 || trace[6] == 1) && (mode != 3 || trace[7] == 1));
    REQUIRE(!sigstat(process.identity, &signal) && !signal.depth && !sigcheck());
    while ((error = procclean(pid)) > 0) {
    }
    REQUIRE(!error && !lifecollect(process.life, 1, &result));
    for (i = 0; i < 128; i++)
      REQUIRE(procwork(32) >= 0 && prepwork(32) >= 0);
    REQUIRE(!setstat(set, &account) && !account.charged && !account.instances);
  }
  REQUIRE(!setclose(set));
  pgstat(&after);
  REQUIRE(before.allocated == after.allocated && before.free == after.free && !pgcheck() &&
          !acctcheck() && !sigcheck() && !lifecheck() && !groupcheck());
  putstr("SUSPENDCHECK cases=");
  puthex(9);
  putstr(" resumed=");
  puthex(5);
  putstr(" terminated=");
  puthex(4);
  putstr(" charged=");
  puthex(0);
  putstr(" allocated=");
  puthex(after.allocated);
  putstr("\n");
}
