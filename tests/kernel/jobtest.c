#include "ktest.h"
#include "load.h"
#include <clock.h>
#include <diag.h>
#include <hal/context.h>
#include <rq.h>
#include <signal.h>
#include <abi/boot.h>
extern const u8          job_image[], job_image_end[];
extern const u64         job_trace_address;
typedef struct Actor     Actor;
typedef struct Jobpacket Jobpacket;
struct Actor
{
  Handle   pid;
  Procinfo info;
};
struct Jobpacket
{
  Boot     header;
  Strref   args[2];
  Resentry resources[3];
  char     strings[32];
};
static Handle selfstop;
static u32    selfseen;
static u32    selfmode, selfcancelled;
#define CHECK(c)                                                                                   \
  do {                                                                                             \
    if (!(c)) {                                                                                    \
      putstr("JOBFAIL line=");                                                                     \
      puthex(__LINE__);                                                                            \
      putstr("\n");                                                                                \
      panic("Run stop/continue");                                                                  \
    }                                                                                              \
  } while (0)
NORETURN void __real_hal_user_enter(Hctx *context);
NORETURN void
__wrap_hal_user_enter(Hctx *context)
{
  Rtable *table = proccurrent(context);
  if (table && table->owner == selfstop && !selfseen) {
    selfseen = 1;
    CHECK(!procstop(selfstop, SIGNAL_STOP));
    if (selfmode == 2) {
      CHECK(!proccontinue(selfstop));
      selfcancelled++;
    }
    proccheckpoint();
  }
  __real_hal_user_enter(context);
}
static void
create(Setid set, Handle parent, Actor *actor)
{
  CHECK(!procnew(set, parent, 128, 256, LOCALS, &actor->pid) &&
        !procinfo(actor->pid, &actor->info));
}
static void
load(Actor *actor, u32 mode, Handle argument, Handle peer)
{
  Jobpacket packet;
  char      digits[21];
  u64       number = (u64) argument;
  u32       i, at = 20, used = 2;
  digits[at] = 0;
  do {
    digits[--at] = (char) ('0' + number % 10);
    number /= 10;
  } while (number);
  for (i = 0; i < sizeof(packet); i++)
    ((u8 *) &packet)[i] = 0;
  packet.header.magic = BOOT_MAGIC;
  packet.header.format = BOOT_FORMAT;
  packet.header.bytes = sizeof(packet);
  packet.header.argc = 2;
  packet.header.argv_off = offsetof(Jobpacket, args);
  packet.header.resource_count = peer ? 3 : 2;
  packet.header.resources_off = offsetof(Jobpacket, resources);
  packet.header.user_lo = BOOT_USER_LO;
  packet.header.user_hi = BOOT_USER_HI;
  packet.args[0].offset = offsetof(Jobpacket, strings);
  packet.args[0].length = 2;
  packet.strings[0] = (char) ('0' + mode);
  packet.args[1].offset = packet.args[0].offset + 2;
  for (i = at; i <= 20; i++)
    packet.strings[used++] = digits[i];
  packet.args[1].length = used - 2;
  packet.resources[0].tag = BOOT_SELF_SPACE;
  packet.resources[0].handle = testresource(proctable(actor->pid), LOCAL_SPACE);
  packet.resources[1].tag = BOOT_SELF_ALLOC;
  packet.resources[1].handle = testresource(proctable(actor->pid), LOCAL_ALLOC);
  packet.resources[2].tag = 0x100;
  packet.resources[2].handle = peer;
  testload(actor->pid, job_image, (u64) (job_image_end - job_image), &packet, sizeof(packet));
  CHECK(!procpublish(actor->pid));
}
static void
trace(Actor *actor, u64 out[8])
{
  CHECK(!spaceread(actor->info.space, job_trace_address, out, 64));
}
static void
release(Actor *actor)
{
  u64 one = 1;
  CHECK(!spacewrite(actor->info.space, job_trace_address + 8, &one, sizeof(one)));
}
static void
step(void)
{
  int error = procrunone();
  CHECK(!error || error == -ERR_WOULD_BLOCK);
  if (error == -ERR_WOULD_BLOCK)
    CHECK(procidle() >= 0);
}
static void
state(Actor *actor, u32 wanted)
{
  u64  started, current;
  Life life;
  CHECK(!clockmono(&started));
  for (;;) {
    CHECK(!procinfo(actor->pid, &actor->info));
    if (actor->info.state == wanted)
      return;
    if (actor->info.state == PROC_EXITING) {
      CHECK(!lifestat(actor->info.life, &life));
      putstr("JOBUSER code=");
      puthex(life.code);
      putstr(" pc=");
      puthex(life.fault.pc);
      putstr("\n");
      panic("job user premature exit");
    }
    CHECK(!clockmono(&current) && current - started < UINT64_C(3000000000));
    step();
  }
}
static void
finish(Actor *actor)
{
  Life life;
  u64  words[8];
  state(actor, PROC_EXITING);
  trace(actor, words);
  CHECK(!lifestat(actor->info.life, &life));
  if (life.reason != LIFE_EXIT || life.code != 42) {
    putstr("JOBUSER code=");
    puthex(life.code);
    putstr("\n");
    panic("job user result");
  }
  CHECK(words[0] == 2 && words[7] == 42);
}
static void
clean(Actor *actor, u32 reason, u32 code, u32 collect)
{
  Liferesult result;
  Life       life;
  int        error;
  CHECK(!lifestat(actor->info.life, &life) && life.state == LIFE_ENDED && life.reason == reason &&
        life.code == code && !life.stopped);
  while ((error = procclean(actor->pid)) > 0) {
  }
  CHECK(!error);
  if (collect)
    CHECK(!lifecollect(actor->info.life, 1, &result) && result.reason == reason &&
          result.code == code);
}
static void
closed(Setid set)
{
  Set budget;
  CHECK(!setstat(set, &budget) && !budget.charged && !budget.instances && !setclose(set));
}
static void
stopped(Actor *actor, u32 ticks)
{
  u64 before[8], after[8], start = procticks();
  trace(actor, before);
  do {
    CHECK(!procinfo(actor->pid, &actor->info) && actor->info.state == PROC_STOPPED &&
          !actor->info.phase && procrunone() == -ERR_WOULD_BLOCK && procidle() >= 0);
  } while (procticks() - start < ticks);
  trace(actor, after);
  CHECK(before[0] == after[0] && before[2] == after[2]);
}
static void
busy(u32 self)
{
  Setid set;
  Actor actor;
  u64   words[8];
  CHECK(!setcreate(512, 1024, 1, &set));
  create(set, 1, &actor);
  CHECK(procstop(actor.pid, SIGNAL_STOP) == -ERR_STATE);
  load(&actor, 0, 0, 0);
  if (self) {
    selfstop = actor.pid;
    selfseen = 0;
    selfmode = self;
    CHECK(!procrunone() && selfseen);
  } else
    CHECK(!procstop(actor.pid, SIGNAL_STOP));
  if (self == 2) {
    CHECK(!procinfo(actor.pid, &actor.info) && actor.info.state == PROC_READY);
  } else
    stopped(&actor, 2);
  trace(&actor, words);
  CHECK(!words[0] && !words[2] && !proccontinue(actor.pid));
  if (self) {
    CHECK(!procrunone());
    trace(&actor, words);
    CHECK(words[0] == 1 && words[2] > 100);
    CHECK(!procstop(actor.pid, SIGNAL_TSTP) && !procstop(actor.pid, SIGNAL_STOP));
    stopped(&actor, 4);
    CHECK(!proccontinue(actor.pid));
  }
  release(&actor);
  finish(&actor);
  clean(&actor, LIFE_EXIT, 42, 1);
  selfstop = 0;
  CHECK(proccontinue(actor.pid) == -ERR_STALE);
  closed(set);
}
static void
events(u32 kind)
{
  Setid   set;
  Actor   actor, sender;
  Handle  notify;
  u64     words[8], now;
  Siginfo signal;
  u32     i;
  CHECK(!setcreate(1024, 2048, 2, &set));
  create(set, 1, &actor);
  load(&actor, kind == 1 ? 2 : 1, kind == 2 ? 1 : 0, 0);
  state(&actor, PROC_BLOCKED);
  CHECK(!procstop(actor.pid, SIGNAL_STOP));
  if (kind == 2) {
    for (i = 0; i < sizeof(signal); i++)
      ((u8 *) &signal)[i] = 0;
    signal.signum = SIGNAL_USR1;
    CHECK(!procsigqueue(actor.pid, &signal));
    stopped(&actor, 2);
    trace(&actor, words);
    CHECK(!words[3]);
  } else if (kind == 1) {
    trace(&actor, words);
    do {
      stopped(&actor, 1);
      CHECK(!clockmono(&now));
    } while (now < words[4]);
  } else {
    create(set, 1, &sender);
    CHECK(!localendpoint(proctable(sender.pid), actor.info.identity, LOCAL_NOTIFY, LOCAL_USE,
                         (u64) 1 << 32, 0, &notify));
    load(&sender, 5, 0, notify);
    finish(&sender);
    CHECK(!procinfo(actor.pid, &actor.info) && actor.info.state == PROC_STOPPED);
    trace(&actor, words);
    CHECK(words[0] == 1);
    clean(&sender, LIFE_EXIT, 42, 1);
  }
  CHECK(!proccontinue(actor.pid) && !procinfo(actor.pid, &actor.info) &&
        actor.info.state == PROC_READY);
  finish(&actor);
  clean(&actor, LIFE_EXIT, 42, 1);
  closed(set);
}
static void
rpc(u32 awakened, u32 kill, u32 cycle)
{
  Setid  set;
  Actor  client, server, controller;
  Handle call, reverse = 0, control;
  u64    words[8];
  u32    count;
  CHECK(!setcreate(2048, 4096, 3, &set));
  create(set, 1, &client);
  create(set, 1, &server);
  CHECK(!localendpoint(proctable(client.pid), server.info.identity, LOCAL_CALL, LOCAL_USE, 0, 0,
                       &call));
  if (cycle)
    CHECK(!localendpoint(proctable(server.pid), client.info.identity, LOCAL_CALL, LOCAL_USE, 0, 0,
                         &reverse));
  load(&server, cycle ? 9 : 4, 0, reverse);
  load(&client, 3, 0, call);
  state(&client, PROC_BLOCKED);
  do {
    step();
    trace(&server, words);
  } while (!words[0]);
  if (awakened) {
    release(&server);
    finish(&server);
    CHECK(!procinfo(client.pid, &client.info) && client.info.state == PROC_READY);
  }
  CHECK(!procstop(client.pid, SIGNAL_STOP));
  if (kill) {
    create(set, 1, &controller);
    CHECK(!localcontrol(proctable(controller.pid), client.info.life,
                        CONTROL_QUERY | CONTROL_TERMINATE, &control));
    load(&controller, 6, client.pid, control);
    finish(&controller);
    trace(&controller, words);
    CHECK(words[6] == PROCESS_WAIT_RESULT);
    if (!awakened) {
      release(&server);
      finish(&server);
      trace(&server, words);
      CHECK(words[3] == RQ_CHECK_DRAINING);
    }
    clean(&client, LIFE_SIGNAL, SIGNAL_KILL, 1);
    clean(&controller, LIFE_EXIT, 42, 1);
  } else {
    if (!awakened) {
      release(&server);
      if (cycle)
        state(&server, PROC_BLOCKED);
      else
        finish(&server);
    }
    CHECK(!procinfo(client.pid, &client.info) && client.info.state == PROC_STOPPED);
    trace(&client, words);
    CHECK(words[0] == 1 && !rqcount(client.pid, client.info.image, &count));
    if (count != (cycle ? 2U : 1U)) {
      putstr("JOBRQ cycle=");
      puthex(cycle);
      putstr(" count=");
      puthex(count);
      putstr("\n");
      panic("stopped request ownership");
    }
    if (cycle) {
      trace(&server, words);
      CHECK(words[5] == 1 && words[6] == 1);
    }
    CHECK(!proccontinue(client.pid) && !procinfo(client.pid, &client.info) &&
          client.info.state == PROC_READY);
    finish(&client);
    if (cycle) {
      finish(&server);
      trace(&server, words);
      CHECK(words[5] == 2);
    }
    clean(&client, LIFE_EXIT, 42, 1);
  }
  clean(&server, LIFE_EXIT, 42, 1);
  closed(set);
}
static void
waiting(u32 kill)
{
  Setid   set;
  Actor   parent, child, controller;
  Handle  control;
  Group   before, after;
  Life    life;
  Sigstat signal;
  u64     words[8];
  CHECK(!setcreate(2048, 4096, 3, &set));
  create(set, 1, &parent);
  create(set, parent.pid, &child);
  load(&parent, kill == 1 ? 8 : 7, child.pid, 0);
  if (kill == 2)
    release(&parent);
  load(&child, 0, 0, 0);
  state(&parent, PROC_BLOCKED);
  CHECK(!procstop(parent.pid, SIGNAL_STOP));
  if (kill == 1) {
    create(set, 1, &controller);
    CHECK(!localcontrol(proctable(controller.pid), parent.info.life,
                        CONTROL_QUERY | CONTROL_TERMINATE, &control));
    load(&controller, 6, parent.pid, control);
    CHECK(!lifestat(parent.info.life, &life) && !groupstat(life.group, &before));
    finish(&controller);
    trace(&controller, words);
    CHECK(words[6] == PROCESS_WAIT_CHILD && !groupstat(life.group, &after) &&
          before.refs == after.refs + 1 && before.processes == after.processes);
    clean(&parent, LIFE_SIGNAL, SIGNAL_KILL, 1);
    clean(&controller, LIFE_EXIT, 42, 1);
    release(&child);
    finish(&child);
    clean(&child, LIFE_EXIT, 42, 1);
  } else {
    CHECK(!procstop(child.pid, SIGNAL_STOP));
    CHECK(!sigstat(parent.info.identity, &signal) &&
          signal.pending == (kill == 2 ? 0 : (u64) 1 << (SIGNAL_CHLD - 1)));
    CHECK(!sigcancel(parent.info.identity, (u64) 1 << (SIGNAL_CHLD - 1)));
    stopped(&parent, 2);
    CHECK(!proccontinue(parent.pid));
    state(&parent, PROC_BLOCKED);
    trace(&parent, words);
    CHECK(words[3] == 1 && words[4] == 2);
    CHECK(!proccontinue(child.pid));
    CHECK(!sigstat(parent.info.identity, &signal) &&
          signal.pending == (kill == 2 ? 0 : (u64) 1 << (SIGNAL_CHLD - 1)));
    CHECK(!sigcancel(parent.info.identity, (u64) 1 << (SIGNAL_CHLD - 1)));
    do {
      step();
      trace(&parent, words);
    } while (words[3] != 2);
    CHECK(!procinfo(parent.pid, &parent.info) && parent.info.state == PROC_BLOCKED);
    release(&child);
    finish(&child);
    CHECK(!sigstat(parent.info.identity, &signal) &&
          signal.pending == ((u64) 1 << (SIGNAL_CHLD - 1)));
    finish(&parent);
    trace(&parent, words);
    CHECK(words[3] == 3 && words[4] == 2 && words[5] == 3 && words[6] == 4);
    CHECK(!lifestat(child.info.life, &life) && !life.waitref);
    clean(&child, LIFE_EXIT, 42, 0);
    clean(&parent, LIFE_EXIT, 42, 1);
  }
  closed(set);
}
static void
send(Actor *actor, u32 signum)
{
  Siginfo info;
  u32     i;
  for (i = 0; i < sizeof(info); i++)
    ((u8 *) &info)[i] = 0;
  info.signum = signum;
  CHECK(!procsigqueue(actor->pid, &info));
}
static void
pending(Actor *actor, u64 mask)
{
  Sigstat signal;
  CHECK(!sigstat(actor->info.identity, &signal) && signal.pending == mask);
}
static void
signalbusy(u32 mode)
{
  Setid set;
  Actor actor;
  Life  life;
  u64   words[8], before;
  CHECK(!setcreate(512, 1024, 1, &set));
  create(set, 1, &actor);
  load(&actor, 0, mode, 0);
  do {
    step();
    trace(&actor, words);
  } while (words[0] != 1 || words[2] < 100);
  if (mode == 1 || mode == 2) {
    send(&actor, SIGNAL_TSTP);
    pending(&actor, (u64) 1 << (SIGNAL_TSTP - 1));
    CHECK(!procinfo(actor.pid, &actor.info) && actor.info.state == PROC_READY);
    release(&actor);
    state(&actor, PROC_STOPPED);
    trace(&actor, words);
    CHECK(words[0] == 1 && !words[3]);
    pending(&actor, 0);
    send(&actor, SIGNAL_CONT);
    if (mode == 2) {
      CHECK(!procinfo(actor.pid, &actor.info) && actor.info.state == PROC_BLOCKED);
      CHECK(procrunone() == -ERR_WOULD_BLOCK && procidle() >= 0);
      trace(&actor, words);
      CHECK(words[0] == 1 && !words[3]);
      send(&actor, SIGNAL_USR1);
    }
  } else if (mode == 5 || mode == 6) {
    send(&actor, SIGNAL_TSTP);
    do {
      step();
      trace(&actor, words);
    } while (mode == 5 && words[5] != 1);
    CHECK(!procinfo(actor.pid, &actor.info) && actor.info.state == PROC_READY &&
          !lifestat(actor.info.life, &life) && !life.stopped && life.sequence == 1);
    pending(&actor, 0);
    CHECK((mode == 5 && words[3] == SIGNAL_TSTP) || (mode == 6 && !words[3]));
    release(&actor);
  } else if (mode == 7) {
    send(&actor, SIGNAL_TSTP);
    pending(&actor, (u64) 1 << (SIGNAL_TSTP - 1));
    send(&actor, SIGNAL_CONT);
    pending(&actor, (u64) 1 << (SIGNAL_CONT - 1));
    send(&actor, SIGNAL_TSTP);
    pending(&actor, (u64) 1 << (SIGNAL_TSTP - 1));
    send(&actor, SIGNAL_CONT);
    pending(&actor, (u64) 1 << (SIGNAL_CONT - 1));
    CHECK(!lifestat(actor.info.life, &life) && !life.stopped && life.sequence == 1);
    release(&actor);
  } else {
    send(&actor, SIGNAL_STOP);
    stopped(&actor, 2);
    pending(&actor, 0);
    send(&actor, SIGNAL_CONT);
    pending(&actor, mode == 3 ? (u64) 1 << (SIGNAL_CONT - 1) : 0);
    CHECK(!procinfo(actor.pid, &actor.info) && actor.info.state == PROC_READY);
    trace(&actor, words);
    before = words[2];
    step();
    trace(&actor, words);
    CHECK(words[2] > before && !words[3]);
    if (mode == 3) {
      send(&actor, SIGNAL_CONT);
      CHECK(!lifestat(actor.info.life, &life) && life.sequence == 3);
    }
    release(&actor);
  }
  finish(&actor);
  trace(&actor, words);
  if (mode == 1)
    CHECK(words[3] == 1);
  if (mode == 2)
    CHECK(words[3] == SIGNAL_USR1 && words[5] == 1 && (words[4] & ((u64) 1 << (SIGNAL_TSTP - 1))));
  if (mode == 3 || mode == 7)
    CHECK(words[3] == SIGNAL_CONT && words[5] == 1 && words[4] == ((u64) 1 << (SIGNAL_CONT - 1)));
  clean(&actor, LIFE_EXIT, 42, 1);
  closed(set);
}
static void
signalkill(void)
{
  Setid set;
  Actor actor;
  CHECK(!setcreate(512, 1024, 1, &set));
  create(set, 1, &actor);
  load(&actor, 1, 0, 0);
  state(&actor, PROC_BLOCKED);
  send(&actor, SIGNAL_WINCH);
  send(&actor, SIGNAL_CHLD);
  pending(&actor, 0);
  CHECK(!procinfo(actor.pid, &actor.info) && actor.info.state == PROC_BLOCKED);
  send(&actor, SIGNAL_STOP);
  stopped(&actor, 2);
  send(&actor, SIGNAL_KILL);
  CHECK(!procinfo(actor.pid, &actor.info) && actor.info.state == PROC_EXITING);
  clean(&actor, LIFE_SIGNAL, SIGNAL_KILL, 1);
  closed(set);
}
static void
recvsignal(u32 first)
{
  Setid  set;
  Actor  actor, sender;
  Handle notify;
  u64    words[8];
  CHECK(!setcreate(1024, 2048, 2, &set));
  create(set, 1, &actor);
  create(set, 1, &sender);
  CHECK(!localendpoint(proctable(sender.pid), actor.info.identity, LOCAL_NOTIFY, LOCAL_USE,
                       (u64) 1 << 32, 0, &notify));
  load(&actor, 1, first == 2 ? 3 : 2, 0);
  state(&actor, PROC_BLOCKED);
  if (first != 2)
    send(&actor, SIGNAL_STOP);
  if (first == 1)
    send(&actor, SIGNAL_USR1);
  load(&sender, 5, 0, notify);
  finish(&sender);
  if (first == 2) {
    do {
      step();
      trace(&actor, words);
    } while (words[6] != 3);
    send(&actor, SIGNAL_USR1);
    release(&actor);
  } else {
    if (!first)
      send(&actor, SIGNAL_USR1);
    stopped(&actor, 2);
    CHECK(!proccontinue(actor.pid));
  }
  finish(&actor);
  trace(&actor, words);
  CHECK(words[5] == 1 && words[6] == (first == 2 ? 3U : 2U));
  clean(&actor, LIFE_EXIT, 42, 1);
  clean(&sender, LIFE_EXIT, 42, 1);
  closed(set);
}
static void
requestsignal(u32 mode, u32 queued)
{
  Setid  set;
  Actor  client, server;
  Handle call;
  u64    words[8];
  u32    count;
  CHECK(!setcreate(1024, 2048, 2, &set));
  create(set, 1, &client);
  create(set, 1, &server);
  CHECK(!localendpoint(proctable(client.pid), server.info.identity,
                       mode == 5 || mode == 6 ? LOCAL_SEND : LOCAL_CALL, LOCAL_USE, 0, 0, &call));
  load(&server, 4, mode == 5 || mode == 6 ? 1 : 0, 0);
  if (queued)
    CHECK(!procstop(server.pid, SIGNAL_STOP));
  load(&client, 3, mode, call);
  state(&client, PROC_BLOCKED);
  if (!queued) {
    do {
      step();
      trace(&server, words);
    } while (!words[0]);
  }
  if (mode != 7)
    CHECK(!procstop(client.pid, SIGNAL_STOP));
  if (mode != 2 && mode != 4 && mode != 6 && mode != 9)
    send(&client, SIGNAL_USR1);
  if (mode == 7) {
    state(&client, PROC_BLOCKED);
    trace(&client, words);
    CHECK(words[1] == 1 && words[5] == 1 && words[6] == 1 && words[4] &&
          !rqcount(client.pid, client.info.image, &count) && count == 1);
  }
  if (queued)
    CHECK(!proccontinue(server.pid));
  if (mode == 5) {
    state(&server, PROC_BLOCKED);
    CHECK(!rqcount(client.pid, client.info.image, &count) && count == 1);
    send(&server, SIGNAL_KILL);
    clean(&server, LIFE_SIGNAL, SIGNAL_KILL, 1);
  } else {
    release(&server);
    finish(&server);
    if (mode != 6) {
      trace(&server, words);
      CHECK(words[3] == RQ_CHECK_LIVE);
    }
    clean(&server, LIFE_EXIT, 42, 1);
  }
  if (mode == 2 || mode == 4 || mode == 6 || mode == 9)
    send(&client, SIGNAL_USR1);
  if (mode != 7) {
    stopped(&client, 2);
    CHECK(!proccontinue(client.pid));
  }
  finish(&client);
  trace(&client, words);
  CHECK(words[5] == 1 && words[6] == 2);
  clean(&client, LIFE_EXIT, 42, 1);
  closed(set);
}
static void
replysignal(void)
{
  Setid  set;
  Actor  client, server, sender;
  Handle call, notify;
  u64    words[8];
  CHECK(!setcreate(2048, 4096, 3, &set));
  create(set, 1, &client);
  create(set, 1, &server);
  create(set, 1, &sender);
  CHECK(!localendpoint(proctable(client.pid), server.info.identity, LOCAL_CALL, LOCAL_USE, 0, 0,
                       &call));
  CHECK(!localendpoint(proctable(sender.pid), server.info.identity, LOCAL_NOTIFY, LOCAL_USE,
                       (u64) 1 << 32, 0, &notify));
  load(&server, 4, 2, 0);
  load(&client, 3, 0, call);
  state(&client, PROC_BLOCKED);
  do {
    step();
    trace(&server, words);
  } while (!words[0]);
  release(&server);
  state(&server, PROC_BLOCKED);
  send(&server, SIGNAL_STOP);
  send(&server, SIGNAL_USR1);
  load(&sender, 5, 0, notify);
  finish(&sender);
  finish(&client);
  stopped(&server, 2);
  CHECK(!proccontinue(server.pid));
  finish(&server);
  trace(&server, words);
  CHECK(words[5] == 1 && words[6] == 1);
  clean(&client, LIFE_EXIT, 42, 1);
  clean(&server, LIFE_EXIT, 42, 1);
  clean(&sender, LIFE_EXIT, 42, 1);
  closed(set);
}
static void
waitsignal(u32 variant)
{
  Setid set;
  Actor parent, child;
  Life  life;
  Group before, after;
  u64   words[8], configuration = variant;
  u32   group = variant == 3 || variant == 4 || variant >= 7;
  CHECK(!setcreate(1024, 2048, 2, &set));
  create(set, 1, &parent);
  create(set, parent.pid, &child);
  load(&parent, 8, child.pid, 0);
  CHECK(
      !spacewrite(parent.info.space, job_trace_address + 8, &configuration, sizeof(configuration)));
  load(&child, 0, 0, 0);
  state(&parent, PROC_BLOCKED);
  CHECK(!lifestat(parent.info.life, &life) && !groupstat(life.group, &before));
  if (variant == 5) {
    send(&child, SIGNAL_STOP);
    CHECK(!procstop(parent.pid, SIGNAL_STOP));
    stopped(&parent, 2);
    CHECK(!proccontinue(parent.pid));
    state(&parent, PROC_BLOCKED);
    trace(&parent, words);
    CHECK(words[3] == SIGNAL_CHLD && words[4] == 2 && words[5] == 1 && words[6] == 1);
    CHECK(!lifestat(child.info.life, &life) && life.stopped && !life.reason && life.sequence == 2);
    send(&child, SIGNAL_CONT);
  } else if (variant == 7 || variant == 8) {
    send(&parent, SIGNAL_USR1);
    step();
    step();
    CHECK(!procinfo(parent.pid, &parent.info) && parent.info.state == PROC_BLOCKED);
  } else {
    CHECK(!procstop(parent.pid, SIGNAL_STOP));
    if (variant != 2 && variant != 6)
      send(&parent, SIGNAL_USR1);
  }
  release(&child);
  finish(&child);
  if (variant == 2 || variant == 6)
    send(&parent, SIGNAL_USR1);
  if (variant == 9) {
    send(&parent, SIGNAL_KILL);
    CHECK(!lifestat(parent.info.life, &life) && !groupstat(life.group, &after) &&
          before.refs == after.refs + 1 && before.processes == after.processes);
    clean(&parent, LIFE_SIGNAL, SIGNAL_KILL, 1);
    clean(&child, LIFE_EXIT, 42, 1);
  } else {
    if (variant != 5 && variant != 7 && variant != 8) {
      stopped(&parent, 2);
      CHECK(!proccontinue(parent.pid));
    }
    finish(&parent);
    trace(&parent, words);
    CHECK(words[5] == (variant == 7 ? 0U : 1U) && words[6] == 2);
    CHECK(!lifestat(parent.info.life, &life) && !groupstat(life.group, &after) &&
          before.refs == after.refs + 1 + group);
    CHECK(!lifestat(child.info.life, &life) && !life.waitref);
    clean(&child, LIFE_EXIT, 42, 0);
    clean(&parent, LIFE_EXIT, 42, 1);
  }
  closed(set);
}
void
jobtest(void)
{
  Pgstat before, after;
  pgstat(&before);
  CHECK(!proctimerstart());
  busy(0);
  busy(1);
  busy(2);
  events(0);
  events(1);
  events(2);
  rpc(0, 0, 0);
  rpc(1, 0, 0);
  rpc(0, 1, 0);
  rpc(1, 1, 0);
  rpc(0, 0, 1);
  waiting(0);
  waiting(1);
  waiting(2);
  signalbusy(0);
  signalbusy(1);
  signalbusy(2);
  signalbusy(3);
  signalbusy(4);
  signalbusy(5);
  signalbusy(6);
  signalbusy(7);
  signalkill();
  recvsignal(0);
  recvsignal(1);
  recvsignal(2);
  requestsignal(1, 0);
  requestsignal(1, 1);
  requestsignal(2, 0);
  requestsignal(3, 0);
  requestsignal(4, 0);
  requestsignal(5, 1);
  requestsignal(6, 0);
  requestsignal(7, 0);
  requestsignal(8, 0);
  requestsignal(9, 0);
  replysignal();
  waitsignal(1);
  waitsignal(2);
  waitsignal(3);
  waitsignal(4);
  waitsignal(5);
  waitsignal(6);
  waitsignal(7);
  waitsignal(8);
  waitsignal(9);
  pgstat(&after);
  CHECK(before.free == after.free && before.allocated == after.allocated && !pgcheck() &&
        !acctcheck() && !lifecheck() && !groupcheck());
  putstr("JOBSCHECK cases=");
  puthex(46);
  putstr(" self=");
  puthex(selfseen);
  putstr(" self_cancel=");
  puthex(selfcancelled);
  putstr(" busy=");
  puthex(1);
  putstr(" notify=");
  puthex(1);
  putstr(" alarm=");
  puthex(1);
  putstr(" suspend=");
  puthex(1);
  putstr(" rpc=");
  puthex(5);
  putstr(" wait=");
  puthex(1);
  putstr(" group=");
  puthex(1);
  putstr(" killed=");
  puthex(3);
  putstr(" signals=");
  puthex(9);
  putstr(" child_jobs=");
  puthex(2);
  putstr(" recv_signal=");
  puthex(3);
  putstr(" request_signal=");
  puthex(10);
  putstr(" reply_signal=");
  puthex(1);
  putstr(" wait_signal=");
  puthex(9);
  putstr(" charged=");
  puthex(0);
  putstr(" allocated=");
  puthex(after.allocated);
  putstr("\n");
}
