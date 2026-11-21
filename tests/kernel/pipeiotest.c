#include "ktest.h"
#include "load.h"
#include <diag.h>
#include <abi/boot.h>

extern const u8  pipeio_image[], pipeio_image_end[];
extern const u64 pipeio_trace_address;

typedef struct Packet Packet;
struct Packet
{
  Boot     header;
  Strref   arg;
  Resentry resources[3];
  char     role[2];
};

#define CHECK(c)                                                                                   \
  do {                                                                                             \
    if (!(c)) {                                                                                    \
      putstr("PIPEIOFAIL line=");                                                                  \
      puthex(__LINE__);                                                                            \
      putstr("\n");                                                                                \
      panic("pipe I/O adapter");                                                                   \
    }                                                                                              \
  } while (0)

static void
testpair(const char *roles)
{
  Setid      set;
  Set        budget;
  Handle     pids[2];
  Procinfo   processes[2];
  Pgstat     before, after;
  Packet     packet;
  Life       life;
  Liferesult result;
  u64        trace[2];
  u32        i, j, ended, turns = 0;
  int        error;
  pgstat(&before);
  CHECK(!setcreate(4096, 8192, 4, &set));
  for (i = 0; i < 2; i++)
    CHECK(!procnew(set, 1, 256, 512, LOCALS, &pids[i]) && !procinfo(pids[i], &processes[i]));
  for (i = 0; i < 2; i++) {
    Rtable *table = proctable(pids[i]);
    for (j = 0; j < sizeof(packet); j++)
      ((u8 *) &packet)[j] = 0;
    packet.header.magic = BOOT_MAGIC;
    packet.header.format = BOOT_FORMAT;
    packet.header.bytes = sizeof(packet);
    packet.header.argc = 1;
    packet.header.argv_off = offsetof(Packet, arg);
    packet.header.resource_count = ARRAY_SIZE(packet.resources);
    packet.header.resources_off = offsetof(Packet, resources);
    packet.header.user_lo = BOOT_USER_LO;
    packet.header.user_hi = BOOT_USER_HI;
    packet.arg.offset = offsetof(Packet, role);
    packet.arg.length = sizeof(packet.role);
    packet.role[0] = roles[i];
    packet.resources[0].tag = BOOT_SELF_SPACE;
    packet.resources[0].handle = testresource(table, LOCAL_SPACE);
    packet.resources[1].tag = BOOT_SELF_ALLOC;
    packet.resources[1].handle = testresource(table, LOCAL_ALLOC);
    packet.resources[2].tag = i ? BOOT_FS_CALL : 256;
    if (i)
      CHECK(!localendpoint(table, processes[0].identity, LOCAL_CALL, LOCAL_USE, 0, 0,
                           &packet.resources[2].handle));
    else
      CHECK(!localobserve(table, OBSERVE_SOURCE, &packet.resources[2].handle));
    testload(pids[i], pipeio_image, (u64) (pipeio_image_end - pipeio_image), &packet,
             sizeof(packet));
    CHECK(!procpublish(pids[i]));
  }
  for (;;) {
    ended = 0;
    for (i = 0; i < 2; i++) {
      CHECK(!lifestat(processes[i].life, &life));
      if (life.state != LIFE_ENDED)
        continue;
      CHECK(!spaceread(processes[i].space, pipeio_trace_address, trace, sizeof(trace)));
      if (life.reason != LIFE_EXIT || life.code != 42 || trace[1]) {
        putstr(i ? "PIPEIOCLIENT line=" : "PIPEIOSERVER line=");
        puthex(trace[1]);
        putstr(" code=");
        puthex(life.code);
        putstr("\n");
      }
      CHECK(life.reason == LIFE_EXIT && life.code == 42 && trace[0] == 7 && !trace[1]);
      ended++;
    }
    if (ended == 2)
      break;
    CHECK(++turns < 256 && !procrunone());
  }
  for (i = 0; i < 2; i++) {
    while ((error = procclean(pids[i])) > 0) {
    }
    CHECK(!error && !lifecollect(processes[i].life, 1, &result));
  }
  CHECK(!setstat(set, &budget) && !budget.charged && !budget.instances && !setclose(set));
  pgstat(&after);
  CHECK(before.free == after.free && before.allocated == after.allocated && !pgcheck() &&
        !acctcheck());
}

void
pipeiotest(void)
{
  testpair("sc");
  testpair("SC");
  testpair("Ii");
}
