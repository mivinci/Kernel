#include "ktest.h"
#include "load.h"
#include <abi/boot.h>
#include <abi/observe.h>
#include <bootimage.h>
#include <diag.h>
#include <signal.h>
#include <rq.h>
typedef struct Packet Packet;
struct Packet
{
  Boot     header;
  Strref   arg;
  Resentry resources[5];
  char     role[2];
};
extern const u8  filecreate_image[], filecreate_image_end[];
extern const u64 filecreate_trace_address;
static Handle    creatorclient;
static u64       injected;
NORETURN void    __real_hal_user_enter(Hctx *context);
static void      deadtest(u32 creating);
static void      resourceendtest(u32 submitted);
static void      bootstraptest(u32 variant);
static void      bootpeertest(u32 mode);
static void      creatorbackendtest(u32 mode);
static void      datarecoverytest(u32 mode);
static void      filewatchexittest(u32 vfs);
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
static void
loadroles(Setid set, const Bootimage *image, Handle *pids, Procinfo *processes, const char *roles,
          u32 count)
{
  Handle call;
  Packet packet;
  Config config;
  u32    i, j, peer;
  for (i = 0; i < count; i++)
    CHECK(!procnew(set, 1, 256, 512, LOCALS, &pids[i]) && !procinfo(pids[i], &processes[i]));
  for (i = 0; i < count; i++) {
    Rtable *table = proctable(pids[i]);
    peer = count == 2 ? 1 - i : (i ? 0 : 1);
    for (j = 0; j < sizeof(packet); j++)
      ((u8 *) &packet)[j] = 0;
    CHECK(!localendpoint(table, processes[peer].identity, LOCAL_CALL, LOCAL_USE, 0, 0, &call));
    packet.header.magic = BOOT_MAGIC;
    packet.header.format = BOOT_FORMAT;
    packet.header.bytes = sizeof(packet);
    packet.header.argc = 1;
    packet.header.argv_off = offsetof(Packet, arg);
    packet.header.resource_count = count == 3 && i ? 4 : 3;
    packet.header.resources_off = offsetof(Packet, resources);
    packet.header.user_lo = BOOT_USER_LO;
    packet.header.user_hi = BOOT_USER_HI;
    packet.arg.offset = offsetof(Packet, role);
    packet.arg.length = 2;
    packet.role[0] = roles[i];
    packet.resources[0].tag = BOOT_SELF_SPACE;
    packet.resources[0].handle = testresource(table, LOCAL_SPACE);
    packet.resources[1].tag = BOOT_SELF_ALLOC;
    packet.resources[1].handle = testresource(table, LOCAL_ALLOC);
    packet.resources[2].tag = 256;
    packet.resources[2].handle = call;
    if (count == 3 && i) {
      CHECK(!localendpoint(table, processes[i == 1 ? 2 : 1].identity, LOCAL_CALL,
                           LOCAL_USE | LOCAL_GRANT, 0, 0, &call));
      packet.resources[3].tag = 257;
      packet.resources[3].handle = call;
    }
    if (i) {
      CHECK(!localobserve(table, OBSERVE_SOURCE, &call));
      packet.resources[packet.header.resource_count].tag = 258;
      packet.resources[packet.header.resource_count++].handle = call;
    }
    testload(pids[i], filecreate_image, (u64) (filecreate_image_end - filecreate_image), &packet,
             sizeof(packet));
    config.entry = image->entry;
    config.stack = 0x3fffff0000UL;
    config.boot = 0x3fff000000UL;
    config.bootsz = sizeof(packet);
    config.sigmask = config.sigdefault = 0;
    CHECK(!procconfigure(pids[i], &config, i ? 0 : 1) && !procpublish(pids[i]));
  }
}
void
filecreatetest(void)
{
  Setid      set;
  Set        budget;
  Procinfo   processes[3];
  Handle     pids[3];
  Bootimage  image;
  Life       life;
  Liferesult result;
  Pgstat     before, after;
  u64        trace[16];
  u32        i, turns = 0, ended;
  int        error;
  pgstat(&before);
  CHECK(!proctimerstart());
  CHECK(!setcreate(4096, 8192, 4, &set) &&
        !bootimagecheck(filecreate_image, (u64) (filecreate_image_end - filecreate_image), &image));
  loadroles(set, &image, pids, processes, "012", 3);
  creatorclient = pids[0];
  injected = 0;
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
  deadtest(0);
  deadtest(1);
  deadtest(2);
  resourceendtest(0);
  resourceendtest(1);
  bootstraptest(0);
  bootstraptest(1);
  bootstraptest(2);
  bootpeertest(0);
  bootpeertest(1);
  bootpeertest(2);
  creatorbackendtest(0);
  creatorbackendtest(1);
  creatorbackendtest(2);
  datarecoverytest(0);
  datarecoverytest(1);
  datarecoverytest(2);
  filewatchexittest(0);
  filewatchexittest(1);
  filewatchexittest(2);
}
static void
filewatchexittest(u32 vfs)
{
  Setid      set;
  Set        budget;
  Procinfo   process[3];
  Handle     pid[3];
  Bootimage  image;
  Life       life;
  Liferesult result;
  Pgstat     before, after;
  u64        trace[16];
  u32        i, count = vfs ? 3 : 2, turns = 0;
  int        error;
  pgstat(&before);
  CHECK(!setcreate(2048, 4096, count, &set) &&
        !bootimagecheck(filecreate_image, (u64) (filecreate_image_end - filecreate_image), &image));
  loadroles(set, &image, pid, process, vfs == 2 ? "OPQ" : vfs ? "LMN" : "JK", count);
  for (i = 0; i < count; i++) {
    CHECK(!procinfo(pid[i], &process[i]));
    while (process[i].state != PROC_EXITING) {
      error = procrunone();
      CHECK((!error || error == -ERR_WOULD_BLOCK) && ++turns < (vfs == 2 ? 4096U : 256U));
      if (error)
        CHECK(procidle() >= 0);
      CHECK(!procinfo(pid[i], &process[i]));
    }
    CHECK(!lifestat(process[i].life, &life) &&
          !spaceread(process[i].space, filecreate_trace_address, trace, sizeof(trace)));
    if (life.reason != LIFE_EXIT || life.code != 42 || trace[15]) {
      putstr("FILEWATCHRESULT actor=");
      puthex(i);
      putstr(" code=");
      puthex(life.code);
      putstr(" line=");
      puthex(trace[15]);
      putstr("\n");
      panic("file connection observation");
    }
    CHECK(trace[0] == 2);
    if (!vfs && i)
      CHECK(trace[2] == 2 && trace[3] == 1);
    if (vfs && i == 2)
      CHECK(trace[1] == 2 && trace[2] == 2 && trace[3] == 1 && trace[5] == 1 &&
            trace[6] == (vfs == 2));
    while ((error = procclean(pid[i])) > 0) {
    }
    CHECK(!error && !lifecollect(process[i].life, 1, &result));
  }
  CHECK(!setstat(set, &budget) && !budget.charged && !budget.instances && !setclose(set));
  pgstat(&after);
  CHECK(before.free == after.free && before.allocated == after.allocated && !pgcheck() &&
        !acctcheck() && !lifecheck());
  if (vfs) {
    putstr(vfs == 2 ? "FILEPREPTIMECHECK cancelled=" : "FILEPREPDEATHCHECK cancelled=");
    puthex(trace[5]);
    putstr(" attached=");
    puthex(trace[3]);
    putstr(" closed=");
    puthex(trace[2]);
    putstr("\n");
    return;
  }
  putstr("FILEWATCHCHECK references=");
  puthex(trace[2]);
  putstr(" drained=");
  puthex(1);
  putstr(" acked=");
  puthex(trace[3]);
  putstr("\n");
}
static void
datarecoverytest(u32 mode)
{
  Setid      set;
  Set        budget;
  Procinfo   info[2];
  Handle     pids[2];
  Bootimage  image;
  Life       life;
  Liferesult result;
  Pgstat     before, after;
  Rqid       request;
  Rqinfo     pending;
  u64        trace[16];
  u32        cleaned = 0, rqcleaned = 0, turns = 0;
  int        error;
  pgstat(&before);
  CHECK(!setcreate(2048, 4096, 2, &set) &&
        !bootimagecheck(filecreate_image, (u64) (filecreate_image_end - filecreate_image), &image));
  loadroles(set, &image, pids, info, mode == 2 ? "HI" : mode ? "FG" : "DE", 2);
  for (;;) {
    if (!cleaned) {
      CHECK(!procinfo(pids[1], &info[1]));
      if (info[1].state == PROC_EXITING) {
        CHECK(!lifestat(info[1].life, &life) && life.reason == LIFE_EXIT && life.code == 42 &&
              !spaceread(info[1].space, filecreate_trace_address, trace, sizeof(trace)) &&
              trace[0] == 2 && trace[1] == 1 && !trace[2] && trace[8] == (mode == 2 ? 2U : 1U) &&
              !trace[15]);
        while ((error = procclean(pids[1])) > 0) {
        }
        CHECK(!error && !lifecollect(info[1].life, 1, &result));
        cleaned = 1;
      }
    }
    if (cleaned && !rqcleaned) {
      CHECK(!spaceread(info[0].space, filecreate_trace_address, trace, sizeof(trace)));
      if (trace[5] && trace[6] == 11) {
        CHECK(!rqlookup((Handle) trace[5], info[0].identity, &request) &&
              !rqstat(request, &pending) && pending.caller == pids[0] &&
              pending.server == pids[1] && pending.status == -ERR_PEER &&
              !(pending.flags & RQ_FINISHED) && !rqclean(request, info[1].identity));
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
  CHECK(cleaned && rqcleaned && !lifestat(info[0].life, &life) && life.reason == LIFE_EXIT &&
        life.code == 42 &&
        !spaceread(info[0].space, filecreate_trace_address, trace, sizeof(trace)) &&
        trace[0] == 2 && trace[1] == 1 && trace[2] == 1 && trace[3] == 1 &&
        trace[4] == (u64) (mode == 2) && trace[8] && trace[8] < 64 && !trace[15]);
  while ((error = procclean(pids[0])) > 0) {
  }
  CHECK(!error && !lifecollect(info[0].life, 1, &result) && !setstat(set, &budget) &&
        !budget.charged && !budget.instances && !setclose(set));
  pgstat(&after);
  CHECK(before.free == after.free && before.allocated == after.allocated && !pgcheck() &&
        !acctcheck());
  putstr("DATARECOVERCHECK mode=");
  puthex(mode);
  putstr(" fake_done_ignored=");
  puthex(1);
  putstr(" quarantined=");
  puthex(1);
  putstr(" trusted_cleanup=");
  puthex(rqcleaned);
  putstr(" pins_returned=");
  puthex(mode == 2 ? 2 : 1);
  putstr(" success_preserved=");
  puthex(mode == 2);
  putstr(" charged=");
  puthex(0);
  putstr(" allocated=");
  puthex(after.allocated);
  putstr("\n");
}
static void
creatorbackendtest(u32 mode)
{
  Setid      set;
  Set        budget;
  Procinfo   info[3];
  Handle     pids[3];
  Bootimage  image;
  Life       life;
  Liferesult result;
  Pgstat     before, after;
  Rqid       request;
  Rqinfo     pending;
  u64        trace[16];
  u32        i, cleaned = 0, rqcleaned = 0, turns = 0, caller = mode ? 0 : 1;
  int        error;
  pgstat(&before);
  CHECK(!setcreate(4096, 8192, 4, &set) &&
        !bootimagecheck(filecreate_image, (u64) (filecreate_image_end - filecreate_image), &image));
  loadroles(set, &image, pids, info, mode == 2 ? "ABC" : mode ? "xyz" : "uvw", 3);
  for (;;) {
    if (!cleaned) {
      CHECK(!procinfo(pids[2], &info[2]));
      if (info[2].state == PROC_EXITING) {
        CHECK(!lifestat(info[2].life, &life) && life.reason == LIFE_EXIT && life.code == 42 &&
              !spaceread(info[2].space, filecreate_trace_address, trace, sizeof(trace)) &&
              trace[0] == 2 && trace[1] == (mode == 2 ? 2U : 1U) && !trace[2] &&
              trace[3] == (mode == 2 ? 2U
                           : mode    ? 1U
                                     : 0U) &&
              trace[4] == 1 && !trace[15]);
        while ((error = procclean(pids[2])) > 0) {
        }
        CHECK(!error && !lifecollect(info[2].life, 1, &result));
        cleaned = 1;
      }
    }
    if (cleaned && !rqcleaned) {
      CHECK(!spaceread(info[caller].space, filecreate_trace_address, trace, sizeof(trace)));
      if (trace[5] && (!mode || trace[6] == 10)) {
        CHECK(!rqlookup((Handle) trace[5], info[caller].identity, &request) &&
              !rqstat(request, &pending) && pending.caller == pids[caller] &&
              pending.server == pids[2] && pending.status == -ERR_PEER &&
              !(pending.flags & RQ_FINISHED) && !rqclean(request, info[2].identity));
        rqcleaned = 1;
      }
    }
    CHECK(!procinfo(pids[0], &info[0]) && !procinfo(pids[1], &info[1]));
    if (info[0].state == PROC_EXITING && info[1].state == PROC_EXITING)
      break;
    error = procrunone();
    CHECK(!error || error == -ERR_WOULD_BLOCK);
    CHECK(++turns < 256);
    if (error)
      CHECK(procidle() >= 0);
  }
  CHECK(cleaned && rqcleaned);
  for (i = 0; i < 2; i++) {
    CHECK(!lifestat(info[i].life, &life) && life.reason == LIFE_EXIT && life.code == 42 &&
          !spaceread(info[i].space, filecreate_trace_address, trace, sizeof(trace)) &&
          trace[0] == 2 && !trace[15]);
    if (!i)
      CHECK(trace[1] == (mode == 2 ? 2U : 1U) && trace[2] == 1 && trace[3] == 1 &&
            trace[4] == (u64) (mode == 2) && (mode ? trace[8] && trace[8] < 64 : !trace[8]));
    else
      CHECK(!trace[1] && !trace[2] && trace[4] == (mode == 2 ? 2U : 1U));
    while ((error = procclean(pids[i])) > 0) {
    }
    CHECK(!error && !lifecollect(info[i].life, 1, &result));
  }
  CHECK(!setstat(set, &budget) && !budget.charged && !budget.instances && !setclose(set));
  pgstat(&after);
  CHECK(before.free == after.free && before.allocated == after.allocated && !pgcheck() &&
        !acctcheck());
  putstr("CREATEBACKENDCHECK mode=");
  puthex(mode);
  putstr(" reference_ended=");
  puthex(1);
  putstr(" attach_quarantined=");
  puthex(mode != 0);
  putstr(" trusted_cleanup=");
  puthex(rqcleaned);
  putstr(" old_file_ended=");
  puthex(mode == 2);
  putstr(" vfs_usable=");
  puthex(1);
  putstr(" charged=");
  puthex(0);
  putstr(" allocated=");
  puthex(after.allocated);
  putstr("\n");
}
static void
bootpeertest(u32 mode)
{
  Setid      set;
  Set        budget;
  Procinfo   info[2];
  Handle     pids[2];
  Bootimage  image;
  Life       life;
  Liferesult result;
  Pgstat     before, after;
  Rqid       request;
  Rqinfo     pending;
  u64        trace[16], gate = 1;
  u32        cleaned = 0, rqcleaned = 0, turns = 0;
  char       roles[3] = {'o', 'r', 0};
  int        error;
  roles[0] += (char) mode;
  roles[1] += (char) mode;
  pgstat(&before);
  CHECK(!setcreate(2048, 4096, 2, &set) &&
        !bootimagecheck(filecreate_image, (u64) (filecreate_image_end - filecreate_image), &image));
  loadroles(set, &image, pids, info, roles, 2);
  for (;;) {
    if (!cleaned) {
      CHECK(!procinfo(pids[1], &info[1]));
      if (info[1].state == PROC_EXITING) {
        CHECK(!lifestat(info[1].life, &life) && life.reason == LIFE_EXIT && life.code == 42 &&
              !spaceread(info[1].space, filecreate_trace_address, trace, sizeof(trace)) &&
              trace[0] == 2 && trace[1] == (u64) (mode != 0) && !trace[15]);
        while ((error = procclean(pids[1])) > 0) {
        }
        CHECK(!error && !lifecollect(info[1].life, 1, &result));
        cleaned = 1;
        if (!mode)
          CHECK(!spacewrite(info[0].space, filecreate_trace_address + 7 * 8, &gate, 8));
      }
    }
    if (mode && cleaned && !rqcleaned) {
      CHECK(!spaceread(info[0].space, filecreate_trace_address, trace, sizeof(trace)));
      if (trace[5] && trace[6] == 9) {
        CHECK(!rqlookup((Handle) trace[5], info[0].identity, &request) &&
              !rqstat(request, &pending) && pending.caller == pids[0] &&
              pending.server == pids[1] && pending.status == -ERR_PEER &&
              !(pending.flags & RQ_FINISHED) && !rqclean(request, info[1].identity));
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
  CHECK(cleaned && rqcleaned == (u32) (mode != 0) && !lifestat(info[0].life, &life) &&
        life.reason == LIFE_EXIT && life.code == 42 &&
        !spaceread(info[0].space, filecreate_trace_address, trace, sizeof(trace)) &&
        trace[0] == 2 && trace[1] == mode && trace[2] == 1 && trace[3] == 1 && !trace[15] &&
        (mode ? trace[4] && trace[4] < 64 : !trace[4]));
  while ((error = procclean(pids[0])) > 0) {
  }
  CHECK(!error && !lifecollect(info[0].life, 1, &result) && !setstat(set, &budget) &&
        !budget.charged && !budget.instances && !setclose(set));
  pgstat(&after);
  CHECK(before.free == after.free && before.allocated == after.allocated && !pgcheck() &&
        !acctcheck());
  putstr("BOOTPEERCHECK mode=");
  puthex(mode);
  putstr(" quarantined=");
  puthex(mode != 0);
  putstr(" trusted_cleanup=");
  puthex(rqcleaned);
  putstr(" old_mapping=");
  puthex(mode == 2);
  putstr(" charged=");
  puthex(0);
  putstr(" allocated=");
  puthex(after.allocated);
  putstr("\n");
}
static void
deadtest(u32 creating)
{
  Setid      set;
  Set        budget;
  Procinfo   info[2];
  Handle     pids[2];
  Bootimage  image;
  Life       life;
  Liferesult result;
  Pgstat     before, after;
  u64        trace[16];
  u32        cleaned = 0, rqcleaned = 0, turns = 0;
  char       roles[2];
  Rqid       request;
  Rqinfo     pending;
  int        error;
  pgstat(&before);
  CHECK(!setcreate(4096, 8192, 4, &set) &&
        !bootimagecheck(filecreate_image, (u64) (filecreate_image_end - filecreate_image), &image));
  roles[0] = (char) ('3' + creating * 2);
  roles[1] = (char) (roles[0] + 1);
  loadroles(set, &image, pids, info, roles, 2);
  for (;;) {
    if (!cleaned) {
      CHECK(!procinfo(pids[1], &info[1]));
      if (info[1].state == PROC_EXITING) {
        CHECK(!lifestat(info[1].life, &life) && life.reason == LIFE_EXIT && life.code == 42 &&
              !spaceread(info[1].space, filecreate_trace_address, trace, sizeof(trace)) &&
              trace[0] == 2 && trace[1] == (creating ? 1 : 2) && trace[2] == (creating ? 0 : 1) &&
              !trace[15]);
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
        trace[1] == (creating ? 1 : 2) && trace[2] == (creating == 2 ? 3 : 2) && trace[3] == 1 &&
        trace[4] < 64 && !trace[15] &&
        (!creating || (trace[7] == 1 && trace[8] == (u64) -ERR_PEER)));
  while ((error = procclean(pids[0])) > 0) {
  }
  CHECK(!error && !lifecollect(info[0].life, 1, &result) && !setstat(set, &budget) &&
        !budget.charged && !budget.instances && !setclose(set));
  pgstat(&after);
  CHECK(before.free == after.free && before.allocated == after.allocated && !pgcheck() &&
        !acctcheck());
  putstr(creating ? "CREATEDEADCHECK actors=" : "FILEDEADCHECK actors=");
  puthex(2);
  if (creating) {
    putstr(" opcode=");
    puthex(creating == 1 ? 2 : 24);
    putstr(" attempts=");
    puthex(2);
    putstr(" submitted=");
    puthex(1);
    putstr(" slots=");
    puthex(creating == 1 ? 2 : 3);
    putstr(" call_failed=");
    puthex(1);
  } else {
    putstr(" references=");
    puthex(2);
    putstr(" accepted_close=");
    puthex(1);
  }
  putstr(" recovered=");
  puthex(creating == 2 ? 3 : 2);
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
static void
bootstraptest(u32 variant)
{
  Setid      set;
  Set        budget;
  Procinfo   info[3];
  Handle     pids[3];
  Bootimage  image;
  Life       life;
  Liferesult result;
  Pgstat     before, after;
  u64        trace[16];
  u32        i, ended, turns = 0;
  int        error;
  pgstat(&before);
  CHECK(!setcreate(4096, 8192, 4, &set) &&
        !bootimagecheck(filecreate_image, (u64) (filecreate_image_end - filecreate_image), &image));
  loadroles(set, &image, pids, info, variant == 2 ? "lmn" : variant ? "ijk" : "cde", 3);
  do {
    ended = 0;
    for (i = 0; i < 3; i++) {
      CHECK(!procinfo(pids[i], &info[i]));
      if (info[i].state == PROC_EXITING)
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
    CHECK(!lifestat(info[i].life, &life) && life.reason == LIFE_EXIT && life.code == 42 &&
          !spaceread(info[i].space, filecreate_trace_address, trace, sizeof(trace)) &&
          trace[0] == 2 && !trace[15]);
    if (!i)
      CHECK(trace[1] == (variant == 2 ? 3
                         : variant    ? 1
                                      : 2) &&
            trace[2] == (variant == 1 ? 1 : 2) && trace[3] == 1 && (variant != 2 || trace[4] == 1));
    else if (i == 1)
      CHECK(!trace[1] && !trace[2] && trace[4] == (variant == 2 ? 3 : variant ? 1 : 2));
    else
      CHECK(trace[1] == (variant == 2 ? 3
                         : variant    ? 1
                                      : 2) &&
            trace[2] == (variant == 2 ? 3
                         : variant    ? 1
                                      : 2) &&
            trace[3] == (variant == 2 ? 3
                         : variant    ? 1
                                      : 2) &&
            trace[4] == (variant == 2 ? 2 : 1));
    while ((error = procclean(pids[i])) > 0) {
    }
    CHECK(!error && !lifecollect(info[i].life, 1, &result));
  }
  CHECK(!setstat(set, &budget) && !budget.charged && !budget.instances && !setclose(set));
  pgstat(&after);
  CHECK(before.free == after.free && before.allocated == after.allocated && !pgcheck() &&
        !acctcheck());
  if (variant == 2) {
    putstr("REUSEABORTCHECK actors=");
    puthex(3);
    putstr(" attempts=");
    puthex(2);
    putstr(" business_error=");
    puthex(1);
    putstr(" unexpected_resource=");
    puthex(1);
    putstr(" old_file_closed=");
    puthex(1);
    putstr(" mem_preserved=");
    puthex(1);
    putstr(" call_preserved=");
    puthex(1);
    putstr(" mapping_preserved=");
    puthex(1);
    putstr(" charged=");
    puthex(0);
    putstr(" allocated=");
    puthex(after.allocated);
    putstr("\n");
    return;
  }
  if (variant) {
    putstr("MAPDISCARDCHECK actors=");
    puthex(3);
    putstr(" bad_header=");
    puthex(1);
    putstr(" aborted=");
    puthex(1);
    putstr(" unmapped=");
    puthex(1);
    putstr(" mem_released=");
    puthex(1);
    putstr(" call_retained=");
    puthex(1);
    putstr(" draining=");
    puthex(1);
    putstr(" charged=");
    puthex(0);
    putstr(" allocated=");
    puthex(after.allocated);
    putstr("\n");
    return;
  }
  putstr("MAPSALVAGECHECK actors=");
  puthex(3);
  putstr(" native_map_conflict=");
  puthex(1);
  putstr(" aborted=");
  puthex(1);
  putstr(" retained_transport=");
  puthex(1);
  putstr(" reused=");
  puthex(1);
  putstr(" closed=");
  puthex(2);
  putstr(" charged=");
  puthex(0);
  putstr(" allocated=");
  puthex(after.allocated);
  putstr("\n");
}
static void
resourceendtest(u32 submitted)
{
  Setid      set;
  Set        budget;
  Procinfo   info[3];
  Handle     pids[3];
  Bootimage  image;
  Life       life;
  Liferesult result;
  Pgstat     before, after;
  Rqid       request;
  Rqinfo     pending;
  u64        trace[16];
  u32        i, cleaned = 0, rqcleaned = 0, turns = 0, caller = submitted ? 0 : 1;
  int        error;
  pgstat(&before);
  CHECK(!setcreate(4096, 8192, 4, &set) &&
        !bootimagecheck(filecreate_image, (u64) (filecreate_image_end - filecreate_image), &image));
  loadroles(set, &image, pids, info, submitted ? "fgh" : "9ab", 3);
  for (;;) {
    if (!cleaned) {
      CHECK(!procinfo(pids[2], &info[2]));
      if (info[2].state == PROC_EXITING) {
        CHECK(!lifestat(info[2].life, &life) && life.reason == LIFE_EXIT && life.code == 42 &&
              !spaceread(info[2].space, filecreate_trace_address, trace, sizeof(trace)) &&
              trace[0] == 2 && trace[1] == 1 && !trace[2] && trace[4] == 1 && !trace[15]);
        while ((error = procclean(pids[2])) > 0) {
        }
        CHECK(!error && !lifecollect(info[2].life, 1, &result));
        cleaned = 1;
      }
    }
    if (cleaned && !rqcleaned) {
      CHECK(!spaceread(info[caller].space, filecreate_trace_address, trace, sizeof(trace)));
      if (trace[5] && (!submitted || trace[6] == 6)) {
        CHECK(!rqlookup((Handle) trace[5], info[caller].identity, &request) &&
              !rqstat(request, &pending) && pending.caller == pids[caller] &&
              pending.server == pids[2] && pending.status == -ERR_PEER &&
              !(pending.flags & RQ_FINISHED) && !rqclean(request, info[2].identity));
        rqcleaned = 1;
      }
    }
    CHECK(!procinfo(pids[0], &info[0]) && !procinfo(pids[1], &info[1]));
    if (info[0].state == PROC_EXITING && info[1].state == PROC_EXITING)
      break;
    error = procrunone();
    CHECK(!error || error == -ERR_WOULD_BLOCK);
    CHECK(++turns < 256);
    if (error)
      CHECK(procidle() >= 0);
  }
  CHECK(cleaned && rqcleaned);
  for (i = 0; i < 2; i++) {
    CHECK(!lifestat(info[i].life, &life) && life.reason == LIFE_EXIT && life.code == 42 &&
          !spaceread(info[i].space, filecreate_trace_address, trace, sizeof(trace)) &&
          trace[0] == 2 && !trace[15]);
    if (!i)
      CHECK(trace[1] == 1 && trace[2] == 1 && trace[3] == 1 &&
            (!submitted || (trace[4] && trace[4] < 64)));
    else
      CHECK(!trace[1] && !trace[2] && trace[4] == 1);
    while ((error = procclean(pids[i])) > 0) {
    }
    CHECK(!error && !lifecollect(info[i].life, 1, &result));
  }
  CHECK(!setstat(set, &budget) && !budget.charged && !budget.instances && !setclose(set));
  pgstat(&after);
  CHECK(before.free == after.free && before.allocated == after.allocated && !pgcheck() &&
        !acctcheck());
  putstr("RESOURCEDEADCHECK actors=");
  puthex(3);
  putstr(" resource_opens=");
  puthex(1);
  putstr(" recovered=");
  puthex(1);
  putstr(" full_directory=");
  puthex(1);
  putstr(" failed_abort=");
  puthex(1);
  putstr(" submitted_abort=");
  puthex(submitted);
  putstr(" quarantined=");
  puthex(submitted);
  putstr(" source_usable=");
  puthex(1);
  putstr(" trusted_cleanup=");
  puthex(1);
  putstr(" charged=");
  puthex(0);
  putstr(" allocated=");
  puthex(after.allocated);
  putstr("\n");
}
