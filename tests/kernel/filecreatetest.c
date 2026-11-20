#include "ktest.h"
#include "load.h"
#include <abi/boot.h>
#include <bootimage.h>
#include <diag.h>
#include <signal.h>
#include <rq.h>
typedef struct Packet Packet;
struct Packet
{
  Boot     header;
  Strref   arg;
  Resentry resources[4];
  char     role[2];
};
extern const u8  filecreate_image[], filecreate_image_end[];
extern const u64 filecreate_trace_address;
static Handle    creatorclient;
static u64       injected;
NORETURN void    __real_hal_user_enter(Hctx *context);
static void      deadtest(void);
#define CHECK(c)                                                                                   \
  do {                                                                                             \
    if (!(c)) {                                                                                    \
      putstr("FILECREATEFAIL line=");                                                              \
      puthex(__LINE__);                                                                            \
      putstr("\n");                                                                                \
      panic("file creator RPC");                                                                   \
    }                                                                                              \
  } while (0)
NORETURN void
__wrap_hal_user_enter(Hctx *context)
{
  Rtable  *table = proccurrent(context);
  Procinfo process;
  Siginfo  info;
  u64      trace[16], stage;
  u32      i;
  if (table && table->owner == creatorclient) {
    CHECK(!procinfo(creatorclient, &process) &&
          !spaceread(process.space, filecreate_trace_address, trace, sizeof(trace)));
    stage = trace[6];
    if ((stage == 1 || stage == 2) && context->tf.pc == trace[7] && !(injected & (1UL << stage))) {
      injected |= 1UL << stage;
      for (i = 0; i < sizeof(info); i++)
        ((u8 *) &info)[i] = 0;
      info.signum = SIGNAL_USR1;
      CHECK(!procsigqueue(creatorclient, &info));
      procuserreturn(context);
    }
  }
  __real_hal_user_enter(context);
}
void
filecreatetest(void)
{
  Setid      set;
  Set        budget;
  Procinfo   processes[3];
  Handle     pids[3], call;
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
  for (i = 0; i < 3; i++)
    CHECK(!procnew(set, 1, 256, 512, LOCALS, &pids[i]) && !procinfo(pids[i], &processes[i]));
  creatorclient = pids[0];
  injected = 0;
  for (i = 0; i < 3; i++) {
    Rtable *table = proctable(pids[i]);
    for (j = 0; j < sizeof(packet); j++)
      ((u8 *) &packet)[j] = 0;
    CHECK(!localendpoint(table, processes[i ? 0 : 1].identity, LOCAL_CALL, LOCAL_USE, 0, 0, &call));
    packet.header.magic = BOOT_MAGIC;
    packet.header.format = BOOT_FORMAT;
    packet.header.bytes = sizeof(packet);
    packet.header.argc = 1;
    packet.header.argv_off = offsetof(Packet, arg);
    packet.header.resource_count = i ? 4 : 3;
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
    if (i) {
      CHECK(!localendpoint(table, processes[i == 1 ? 2 : 1].identity, LOCAL_CALL,
                           LOCAL_USE | LOCAL_GRANT, 0, 0, &call));
      packet.resources[3].tag = 257;
      packet.resources[3].handle = call;
    }
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
    for (i = 0; i < 3; i++) {
      CHECK(!procinfo(pids[i], &processes[i]));
      if (processes[i].state == PROC_EXITING)
        ended++;
    }
    if (ended == 3)
      break;
    error = procrunone();
    CHECK(!error || error == -ERR_WOULD_BLOCK);
    CHECK(++turns < 256);
    if (error)
      CHECK(procidle() >= 0);
  } while (1);
  for (i = 0; i < 3; i++) {
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
    CHECK(trace[0] == 2);
    if (!i)
      CHECK(trace[1] == 11 && trace[2] == 9 && trace[3] == 1 && trace[4] == 3 && trace[5] == 1 &&
            trace[10] == 2 && trace[11] == 1 && trace[12] == 1 && injected == 6);
    else if (i == 1)
      CHECK(trace[1] == 7 && trace[2] == 7 && trace[4] == 4);
    else
      CHECK(trace[1] == 4 && trace[2] == 4 && trace[3] == 3 && trace[4] == 3);
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
  puthex(3);
  putstr(" requests=");
  puthex(11);
  putstr(" references=");
  puthex(11);
  putstr(" closed=");
  puthex(11);
  putstr(" creator_signals=");
  puthex(2);
  putstr(" creator_captured=");
  puthex(1);
  putstr(" creator_waiting=");
  puthex(1);
  putstr(" resource_opens=");
  puthex(4);
  putstr(" attached=");
  puthex(3);
  putstr(" revoked=");
  puthex(2);
  putstr(" abort_requests=");
  puthex(3);
  putstr(" abandoned=");
  puthex(1);
  putstr(" errors=");
  puthex(1);
  putstr(" charged=");
  puthex(0);
  putstr(" allocated=");
  puthex(after.allocated);
  putstr("\n");
  deadtest();
}
static void
deadtest(void)
{
  Setid      set;
  Set        budget;
  Procinfo   info[2];
  Handle     pids[2], call;
  Packet     packet;
  Bootimage  image;
  Config     config;
  Life       life;
  Liferesult result;
  Pgstat     before, after;
  u64        trace[16];
  u32        i, j, cleaned = 0, rqcleaned = 0, turns = 0;
  Rqid       request;
  Rqinfo     pending;
  int        error;
  pgstat(&before);
  CHECK(!setcreate(4096, 8192, 4, &set) &&
        !bootimagecheck(filecreate_image, (u64) (filecreate_image_end - filecreate_image), &image));
  for (i = 0; i < 2; i++)
    CHECK(!procnew(set, 1, 256, 512, LOCALS, &pids[i]) && !procinfo(pids[i], &info[i]));
  for (i = 0; i < 2; i++) {
    Rtable *table = proctable(pids[i]);
    for (j = 0; j < sizeof(packet); j++)
      ((u8 *) &packet)[j] = 0;
    CHECK(!localendpoint(table, info[1 - i].identity, LOCAL_CALL, LOCAL_USE, 0, 0, &call));
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
    packet.role[0] = (char) ('3' + i);
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
  for (;;) {
    if (!cleaned) {
      CHECK(!procinfo(pids[1], &info[1]));
      if (info[1].state == PROC_EXITING) {
        CHECK(!lifestat(info[1].life, &life) && life.reason == LIFE_EXIT && life.code == 42 &&
              !spaceread(info[1].space, filecreate_trace_address, trace, sizeof(trace)) &&
              trace[0] == 2 && trace[1] == 2 && trace[2] == 1 && !trace[15]);
        while ((error = procclean(pids[1])) > 0) {
        }
        CHECK(!error && !lifecollect(info[1].life, 1, &result));
        cleaned = 1;
      }
    }
    if (cleaned && !rqcleaned) {
      CHECK(!spaceread(info[0].space, filecreate_trace_address, trace, sizeof(trace)));
      if (trace[6] == 6) {
        CHECK(!rqlookup((Handle) trace[5], info[0].identity, &request) &&
              !rqstat(request, &pending) && pending.caller == pids[0] &&
              pending.server == pids[1] && pending.status == -ERR_PEER &&
              !(pending.flags & RQ_FINISHED));
        /* Trusted fixture manager: this server has no downstream/DMA and its
           full address space is gone. Userland init authorization is separate. */
        CHECK(!rqclean(request, info[1].identity) && !rqstat(request, &pending) &&
              pending.status == -ERR_PEER && (pending.flags & RQ_FINISHED));
        rqcleaned = 1;
      }
    }
    CHECK(!procinfo(pids[0], &info[0]));
    if (info[0].state == PROC_EXITING)
      break;
    error = procrunone();
    CHECK(!error || error == -ERR_WOULD_BLOCK);
    CHECK(++turns < 256);
    if (error)
      CHECK(procidle() >= 0);
  }
  CHECK(!lifestat(info[0].life, &life) &&
        !spaceread(info[0].space, filecreate_trace_address, trace, sizeof(trace)));
  if (life.reason != LIFE_EXIT || life.code != 42 || trace[15]) {
    putstr("FILEDEADRESULT code=");
    puthex(life.code);
    putstr(" line=");
    puthex(trace[15]);
    putstr("\n");
  }
  CHECK(cleaned && rqcleaned && life.reason == LIFE_EXIT && life.code == 42 && trace[0] == 2 &&
        trace[1] == 2 && trace[2] == 2 && trace[3] == 1 && trace[4] < 64 && !trace[15]);
  while ((error = procclean(pids[0])) > 0) {
  }
  CHECK(!error && !lifecollect(info[0].life, 1, &result) && !setstat(set, &budget) &&
        !budget.charged && !budget.instances && !setclose(set));
  pgstat(&after);
  CHECK(before.free == after.free && before.allocated == after.allocated && !pgcheck() &&
        !acctcheck());
  putstr("FILEDEADCHECK actors=");
  puthex(2);
  putstr(" references=");
  puthex(2);
  putstr(" accepted_close=");
  puthex(1);
  putstr(" recovered=");
  puthex(2);
  putstr(" quarantined=");
  puthex(1);
  putstr(" trusted_cleanup=");
  puthex(1);
  putstr(" charged=");
  puthex(0);
  putstr(" allocated=");
  puthex(after.allocated);
  putstr("\n");
}
