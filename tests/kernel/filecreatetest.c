#include "ktest.h"
#include "load.h"
#include <abi/boot.h>
#include <bootimage.h>
#include <diag.h>
typedef struct Packet Packet;
struct Packet
{
  Boot     header;
  Strref   arg;
  Resentry resources[3];
  char     role[2];
};
extern const u8  filecreate_image[], filecreate_image_end[];
extern const u64 filecreate_trace_address;
#define CHECK(c)                                                                                   \
  do {                                                                                             \
    if (!(c)) {                                                                                    \
      putstr("FILECREATEFAIL line=");                                                              \
      puthex(__LINE__);                                                                            \
      putstr("\n");                                                                                \
      panic("file creator RPC");                                                                   \
    }                                                                                              \
  } while (0)
void
filecreatetest(void)
{
  Setid      set;
  Set        budget;
  Procinfo   processes[2];
  Handle     pids[2], call;
  Packet     packet;
  Bootimage  image;
  Config     config;
  Life       life;
  Liferesult result;
  Pgstat     before, after;
  u64        trace[16];
  u32        i, j, turns = 0, ended;
  int        error;
  pgstat(&before);
  CHECK(!setcreate(4096, 8192, 4, &set) &&
        !bootimagecheck(filecreate_image, (u64) (filecreate_image_end - filecreate_image), &image));
  for (i = 0; i < 2; i++)
    CHECK(!procnew(set, 1, 256, 512, LOCALS, &pids[i]) && !procinfo(pids[i], &processes[i]));
  for (i = 0; i < 2; i++) {
    Rtable *table = proctable(pids[i]);
    for (j = 0; j < sizeof(packet); j++)
      ((u8 *) &packet)[j] = 0;
    CHECK(!localendpoint(table, processes[1 - i].identity, LOCAL_CALL, LOCAL_USE, 0, 0, &call));
    packet.header.magic = BOOT_MAGIC;
    packet.header.format = BOOT_FORMAT;
    packet.header.bytes = sizeof(packet);
    packet.header.argc = 1;
    packet.header.argv_off = offsetof(Packet, arg);
    packet.header.resource_count = 3;
    packet.header.resources_off = offsetof(Packet, resources);
    packet.header.user_lo = BOOT_USER_LO;
    packet.header.user_hi = BOOT_USER_HI;
    packet.arg.offset = offsetof(Packet, role);
    packet.arg.length = 2;
    packet.role[0] = (char) ('0' + i);
    packet.resources[0].tag = BOOT_SELF_SPACE;
    packet.resources[0].handle = testresource(table, LOCAL_SPACE);
    packet.resources[1].tag = BOOT_SELF_ALLOC;
    packet.resources[1].handle = testresource(table, LOCAL_ALLOC);
    packet.resources[2].tag = 256;
    packet.resources[2].handle = call;
    testload(pids[i], filecreate_image, (u64) (filecreate_image_end - filecreate_image), &packet,
             sizeof(packet));
    config.entry = image.entry;
    config.stack = 0x3fffff0000UL;
    config.boot = 0x3fff000000UL;
    config.bootsz = sizeof(packet);
    config.sigmask = config.sigdefault = 0;
    CHECK(!procconfigure(pids[i], &config, i ? 0 : 1) && !procpublish(pids[i]));
  }
  do {
    ended = 0;
    for (i = 0; i < 2; i++) {
      CHECK(!procinfo(pids[i], &processes[i]));
      if (processes[i].state == PROC_EXITING)
        ended++;
    }
    if (ended == 2)
      break;
    error = procrunone();
    CHECK(!error || error == -ERR_WOULD_BLOCK);
    CHECK(++turns < 256);
    if (error)
      CHECK(procidle() >= 0);
  } while (1);
  for (i = 0; i < 2; i++) {
    CHECK(!lifestat(processes[i].life, &life) &&
          !spaceread(processes[i].space, filecreate_trace_address, trace, sizeof(trace)));
    if (life.reason != LIFE_EXIT || life.code != 42 || trace[15]) {
      putstr("FILECREATERESULT actor=");
      puthex(i);
      putstr(" code=");
      puthex(life.code);
      putstr(" line=");
      puthex(trace[15]);
      putstr("\n");
      panic("file creator result");
    }
    CHECK(trace[0] == 2 && trace[1] == 5 && trace[2] == 5 && (i || trace[3] == 1));
    while ((error = procclean(pids[i])) > 0) {
    }
    CHECK(!error && !lifecollect(processes[i].life, 1, &result));
  }
  for (i = 0; i < 256; i++)
    CHECK(procwork(32) >= 0);
  CHECK(!setstat(set, &budget) && !budget.charged && !budget.instances && !setclose(set));
  pgstat(&after);
  CHECK(before.free == after.free && before.allocated == after.allocated && !pgcheck() &&
        !acctcheck() && !lifecheck());
  putstr("FILECREATECHECK actors=");
  puthex(2);
  putstr(" requests=");
  puthex(5);
  putstr(" references=");
  puthex(5);
  putstr(" closed=");
  puthex(5);
  putstr(" abandoned=");
  puthex(1);
  putstr(" errors=");
  puthex(1);
  putstr(" charged=");
  puthex(0);
  putstr(" allocated=");
  puthex(after.allocated);
  putstr("\n");
}
