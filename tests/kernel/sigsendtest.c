#include "ktest.h"
#include "load.h"
#include <clock.h>
#include <diag.h>
#include <signal.h>
#include <abi/boot.h>
extern const u8       send_image[], send_image_end[];
extern const u64      send_trace_address;
typedef struct Actor  Actor;
typedef struct Packet Packet;
struct Actor
{
  Handle   pid;
  Procinfo info;
};
struct Packet
{
  Boot     header;
  Strref   args[2];
  Resentry resources[2];
  char     strings[4];
};
static u64 commands;
#define CHECK(c)                                                                                   \
  do {                                                                                             \
    if (!(c)) {                                                                                    \
      putstr("SIGSENDFAIL line=");                                                                 \
      puthex(__LINE__);                                                                            \
      putstr("\n");                                                                                \
      panic("native signal send");                                                                 \
    }                                                                                              \
  } while (0)
static void
step(void)
{
  int error = procrunone();
  CHECK(!error || error == -ERR_WOULD_BLOCK);
  if (error)
    CHECK(procidle() >= 0);
}
static void
trace(Actor *a, u64 words[16])
{
  CHECK(!spaceread(a->info.space, send_trace_address, words, 128));
}
static void
word(Actor *a, u32 index, u64 value)
{
  CHECK(index < 16 && !spacewrite(a->info.space, send_trace_address + index * 8, &value, 8));
}
static void
state(Actor *a, u32 wanted)
{
  u64  start, now;
  Life life;
  CHECK(!clockmono(&start));
  for (;;) {
    CHECK(!procinfo(a->pid, &a->info));
    if (a->info.state == wanted)
      return;
    if (a->info.state == PROC_EXITING) {
      CHECK(!lifestat(a->info.life, &life));
      putstr("SIGSENDUSER code=");
      puthex(life.code);
      putstr("\n");
      panic("sigsend user premature exit");
    }
    CHECK(!clockmono(&now) && now - start < UINT64_C(3000000000));
    step();
  }
}
static void
started(Actor *a)
{
  u64 words[16], start, now;
  CHECK(!clockmono(&start));
  for (;;) {
    trace(a, words);
    if (words[0] == 1)
      return;
    CHECK(!procinfo(a->pid, &a->info) && a->info.state != PROC_EXITING && !clockmono(&now) &&
          now - start < UINT64_C(3000000000));
    step();
  }
}
static void
create(Setid set, u32 ruid, u32 euid, Actor *a)
{
  Cred cred;
  cred.ruid = ruid;
  cred.euid = euid;
  cred.rgid = cred.egid = 0;
  CHECK(!procnew(set, 1, 128, 256, LOCALS, &a->pid) && !procinfo(a->pid, &a->info) &&
        !lifecredentials(a->info.life, &cred));
}
static void
load(Actor *a, u32 mode, u32 masked)
{
  Packet packet;
  u32    i;
  for (i = 0; i < sizeof(packet); i++)
    ((u8 *) &packet)[i] = 0;
  packet.header.magic = BOOT_MAGIC;
  packet.header.format = BOOT_FORMAT;
  packet.header.bytes = sizeof(packet);
  packet.header.argc = 2;
  packet.header.argv_off = offsetof(Packet, args);
  packet.header.resource_count = 2;
  packet.header.resources_off = offsetof(Packet, resources);
  packet.header.user_lo = BOOT_USER_LO;
  packet.header.user_hi = BOOT_USER_HI;
  packet.args[0].offset = offsetof(Packet, strings);
  packet.args[0].length = 2;
  packet.args[1].offset = offsetof(Packet, strings) + 2;
  packet.args[1].length = 2;
  packet.strings[0] = (char) ('0' + mode);
  packet.strings[2] = (char) ('0' + masked);
  packet.resources[0].tag = BOOT_SELF_SPACE;
  packet.resources[0].handle = testresource(proctable(a->pid), LOCAL_SPACE);
  packet.resources[1].tag = BOOT_SELF_ALLOC;
  packet.resources[1].handle = testresource(proctable(a->pid), LOCAL_ALLOC);
  testload(a->pid, send_image, (u64) (send_image_end - send_image), &packet, sizeof(packet));
  CHECK(!procpublish(a->pid));
}
static void
isolate(Actor *a, u32 separate)
{
  Life   life;
  Group  previous;
  Handle session, group;
  CHECK(!lifestat(a->info.life, &life) && !groupstat(life.group, &previous));
  session = previous.session;
  if (separate)
    CHECK(!sessioncreate(a->pid, &session));
  CHECK(!groupcreate(a->pid, session, &group) && !lifesetgroup(a->info.life, group) &&
        !groupdrop(group));
  if (separate)
    CHECK(!groupdrop(session));
}
static void
join(Actor *a, Actor *leader)
{
  Life life;
  CHECK(!lifestat(leader->info.life, &life) && !lifesetgroup(a->info.life, life.group));
}
static void
clean(Actor *a, u32 reason, u32 code)
{
  Life       life;
  Liferesult result;
  int        error;
  CHECK(!lifestat(a->info.life, &life) && life.state == LIFE_ENDED && life.reason == reason &&
        life.code == code);
  while ((error = procclean(a->pid)) > 0) {
  }
  CHECK(!error && !lifecollect(a->info.life, 1, &result) && result.reason == reason &&
        result.code == code);
}
static void
kill(Actor *a)
{
  Siginfo info;
  u32     i;
  for (i = 0; i < sizeof(info); i++)
    ((u8 *) &info)[i] = 0;
  info.signum = SIGNAL_KILL;
  CHECK(!procsigqueue(a->pid, &info));
  clean(a, LIFE_SIGNAL, SIGNAL_KILL);
}
static void
finish(Actor *a, u32 sender)
{
  u64 words[16];
  word(a, sender ? 14 : 1, 1);
  state(a, PROC_EXITING);
  trace(a, words);
  CHECK(words[0] == 2);
}
static void
closed(Setid set)
{
  Set budget;
  CHECK(!setstat(set, &budget) && !budget.charged && !budget.instances && !setclose(set));
}
static void
pending(Actor *a, u64 bits)
{
  Sigstat signal;
  CHECK(!sigstat(a->info.identity, &signal) && signal.pending == bits);
}
static void
metadata(Actor *a, Actor *sender, u32 uid, u32 signum)
{
  u64 words[16], start, now;
  CHECK(!clockmono(&start));
  for (;;) {
    trace(a, words);
    if (words[8])
      break;
    CHECK(!clockmono(&now) && now - start < UINT64_C(3000000000));
    step();
  }
  CHECK(words[8] == 1 && words[9] == (u64) sender->pid && words[10] == uid &&
        words[11] == SIGNAL_SOURCE_USER && words[12] == signum);
}
static u64
begin(Actor *sender, u64 kind, Handle target, u64 signum, u64 raw)
{
  word(sender, 2, kind);
  word(sender, 3, (u64) target);
  word(sender, 4, signum);
  word(sender, 15, raw);
  word(sender, 1, ++commands);
  return commands;
}
static void
completed(Actor *sender, u64 sequence, isize expected)
{
  u64 words[16], start, now;
  CHECK(!clockmono(&start));
  for (;;) {
    trace(sender, words);
    if (words[7] == sequence)
      break;
    CHECK(!procinfo(sender->pid, &sender->info) && sender->info.state != PROC_EXITING &&
          !clockmono(&now) && now - start < UINT64_C(3000000000));
    step();
  }
  if ((isize) words[6] != expected) {
    putstr("SIGSENDRESULT sequence=");
    puthex(sequence);
    putstr(" got=");
    puthex(words[6]);
    putstr(" expected=");
    puthex((u64) expected);
    putstr("\n");
    panic("sigsend native result");
  }
  CHECK(!words[5]);
}
static void
command(Actor *sender, u64 kind, Handle target, u64 signum, u64 raw, isize expected)
{
  completed(sender, begin(sender, kind, target, signum, raw), expected);
}
static void
credentials(u32 ruid, u32 euid)
{
  Setid set;
  Actor sender, receiver;
  CHECK(!setcreate(1024, 2048, 2, &set));
  create(set, 10, 20, &sender);
  create(set, ruid, euid, &receiver);
  load(&sender, 1, 0);
  load(&receiver, 0, 1);
  started(&sender);
  started(&receiver);
  command(&sender, SIGNAL_SELECT_PID, receiver.pid, SIGNAL_STOP, 0, 0);
  command(&sender, SIGNAL_SELECT_PID, receiver.pid, 0, 0, 0);
  CHECK(!procinfo(receiver.pid, &receiver.info) && receiver.info.state == PROC_STOPPED);
  pending(&receiver, 0);
  command(&sender, SIGNAL_SELECT_PID, receiver.pid, SIGNAL_USR1, 0, 0);
  pending(&receiver, (u64) 1 << (SIGNAL_USR1 - 1));
  CHECK(!procinfo(receiver.pid, &receiver.info) && receiver.info.state == PROC_STOPPED &&
        !proccontinue(receiver.pid));
  finish(&receiver, 0);
  metadata(&receiver, &sender, 20, SIGNAL_USR1);
  command(&sender, SIGNAL_SELECT_PID, receiver.pid, 0, 0, -ERR_STALE);
  finish(&sender, 1);
  clean(&receiver, LIFE_EXIT, 42);
  clean(&sender, LIFE_EXIT, 42);
  closed(set);
}
static void
rootdenied(void)
{
  Setid set;
  Actor sender, receiver;
  CHECK(!setcreate(1024, 2048, 2, &set));
  create(set, 0, 0, &sender);
  create(set, 42, 43, &receiver);
  load(&sender, 1, 0);
  load(&receiver, 0, 1);
  started(&sender);
  started(&receiver);
  command(&sender, SIGNAL_SELECT_PID, receiver.pid, SIGNAL_USR1, 0, -ERR_DENIED);
  command(&sender, SIGNAL_SELECT_PID, receiver.pid, 0, 0, -ERR_DENIED);
  command(&sender, SIGNAL_SELECT_ALL, 0, SIGNAL_USR1, 0, -ERR_DENIED);
  command(&sender, SIGNAL_SELECT_ALL, 0, 0, 0, -ERR_DENIED);
  command(&sender, SIGNAL_SELECT_PID, receiver.pid, SIGNAL_STOP, 0, -ERR_DENIED);
  pending(&receiver, 0);
  CHECK(!procstop(receiver.pid, SIGNAL_STOP));
  command(&sender, SIGNAL_SELECT_PID, receiver.pid, SIGNAL_CONT, 0, 0);
  metadata(&receiver, &sender, 0, SIGNAL_CONT);
  CHECK(!procinfo(receiver.pid, &receiver.info) && receiver.info.state == PROC_READY);
  finish(&receiver, 0);
  finish(&sender, 1);
  clean(&receiver, LIFE_EXIT, 42);
  clean(&sender, LIFE_EXIT, 42);
  closed(set);
}
static void
partial(u32 all)
{
  Setid  set;
  Actor  sender, denied, allowed;
  Handle target;
  u64    kind = all ? SIGNAL_SELECT_ALL : SIGNAL_SELECT_GROUP;
  CHECK(!setcreate(2048, 4096, 3, &set));
  create(set, 10, 20, &sender);
  create(set, 55, 66, &denied);
  create(set, 88, 10, &allowed);
  if (!all) {
    isolate(&denied, 0);
    join(&allowed, &denied);
  }
  target = all ? 0 : denied.pid;
  load(&sender, 1, 0);
  load(&denied, 0, 1);
  started(&sender);
  started(&denied);
  if (!all) {
    command(&sender, kind, target, SIGNAL_USR1, 0, -ERR_DENIED);
    command(&sender, SIGNAL_SELECT_PID, allowed.pid, 0, 0, -ERR_STALE);
  }
  load(&allowed, 0, 1);
  started(&allowed);
  command(&sender, kind, target, 0, 0, 0);
  pending(&allowed, 0);
  pending(&denied, 0);
  command(&sender, kind, target, SIGNAL_USR1, 0, 0);
  pending(&denied, 0);
  pending(&allowed, (u64) 1 << (SIGNAL_USR1 - 1));
  finish(&allowed, 0);
  metadata(&allowed, &sender, 20, SIGNAL_USR1);
  finish(&denied, 0);
  finish(&sender, 1);
  clean(&allowed, LIFE_EXIT, 42);
  clean(&denied, LIFE_EXIT, 42);
  clean(&sender, LIFE_EXIT, 42);
  closed(set);
}
static void
controls(u32 separate)
{
  Setid  set;
  Actor  sender, receiver;
  Handle control, query, allocator;
  CHECK(!setcreate(1024, 2048, 2, &set));
  create(set, 10, 20, &sender);
  create(set, 55, 66, &receiver);
  if (separate)
    isolate(&receiver, 1);
  CHECK(!localcontrol(proctable(sender.pid), receiver.info.life, CONTROL_TERMINATE, &control) &&
        !localcontrol(proctable(sender.pid), receiver.info.life, CONTROL_QUERY, &query));
  allocator = testresource(proctable(sender.pid), LOCAL_ALLOC);
  load(&sender, 1, 0);
  load(&receiver, 0, 1);
  started(&sender);
  started(&receiver);
  if (separate) {
    CHECK(!procstop(receiver.pid, SIGNAL_STOP));
    command(&sender, SIGNAL_SELECT_PID, receiver.pid, SIGNAL_CONT, 0, -ERR_DENIED);
    CHECK(!procinfo(receiver.pid, &receiver.info) && receiver.info.state == PROC_STOPPED);
    command(&sender, SIGNAL_SELECT_CONTROL, control, SIGNAL_CONT, 0, 0);
    metadata(&receiver, &sender, 20, SIGNAL_CONT);
  } else {
    command(&sender, SIGNAL_SELECT_CONTROL, query, SIGNAL_USR1, 0, -ERR_DENIED);
    command(&sender, SIGNAL_SELECT_CONTROL, allocator, 0, 0, -ERR_BAD_HANDLE);
    command(&sender, SIGNAL_SELECT_CONTROL, control, 0, 0, 0);
    pending(&receiver, 0);
    command(&sender, SIGNAL_SELECT_CONTROL, control, SIGNAL_USR1, 0, 0);
  }
  finish(&receiver, 0);
  if (!separate)
    metadata(&receiver, &sender, 20, SIGNAL_USR1);
  command(&sender, SIGNAL_SELECT_CONTROL, control, 0, 0, -ERR_STALE);
  finish(&sender, 1);
  clean(&receiver, LIFE_EXIT, 42);
  clean(&sender, LIFE_EXIT, 42);
  closed(set);
}
static void
malformed(void)
{
  Setid  set;
  Actor  sender;
  Handle stale;
  CHECK(!setcreate(512, 1024, 1, &set));
  create(set, 0, 0, &sender);
  load(&sender, 1, 0);
  started(&sender);
  stale = sender.pid + ((Handle) 1 << 16);
  command(&sender, SIGNAL_SELECT_ALL, 0, SIGNAL_USR1, 0, -ERR_STALE);
  command(&sender, 0, 0, 0, 0, -ERR_INVALID);
  command(&sender, 5, 0, 0, 0, -ERR_INVALID);
  command(&sender, SIGNAL_SELECT_ALL, 1, 0, 0, -ERR_INVALID);
  command(&sender, SIGNAL_SELECT_PID, -1, 0, 0, -ERR_BAD_HANDLE);
  command(&sender, SIGNAL_SELECT_GROUP, 0, 0, 0, -ERR_BAD_HANDLE);
  command(&sender, SIGNAL_SELECT_GROUP, stale, 0, 0, -ERR_STALE);
  command(&sender, SIGNAL_SELECT_PID, stale, 0, 0, -ERR_STALE);
  command(&sender, SIGNAL_SELECT_PID, 0, 0, 2, -ERR_FAULT);
  command(&sender, SIGNAL_SELECT_PID, (Handle) send_trace_address + 1, 0, 2, -ERR_INVALID);
  command(&sender, SIGNAL_SELECT_PID, sender.pid, 5, 0, -ERR_INVALID);
  command(&sender, SIGNAL_SELECT_PID, sender.pid, 32, 0, -ERR_INVALID);
  command(&sender, SIGNAL_SELECT_PID, sender.pid, 33, 0, -ERR_INVALID);
  command(&sender, SIGNAL_SELECT_PID, sender.pid, ~(u64) 0, 0, -ERR_INVALID);
  command(&sender, SIGNAL_SELECT_PID, sender.pid, 0, 1, -ERR_INVALID);
  pending(&sender, 0);
  finish(&sender, 1);
  clean(&sender, LIFE_EXIT, 42);
  closed(set);
}
static void
killing(u32 self)
{
  Setid set;
  Actor sender, receiver;
  CHECK(!setcreate(1024, 2048, 2, &set));
  create(set, self ? 10 : 0, self ? 20 : 0, &sender);
  create(set, self ? 10 : 0, self ? 20 : 0, &receiver);
  if (self) {
    isolate(&sender, 0);
    join(&receiver, &sender);
  }
  load(&sender, 1, 0);
  load(&receiver, 0, 0);
  started(&sender);
  started(&receiver);
  if (self) {
    begin(&sender, SIGNAL_SELECT_GROUP, sender.pid, SIGNAL_KILL, 0);
    state(&sender, PROC_EXITING);
  } else {
    command(&sender, SIGNAL_SELECT_ALL, 0, SIGNAL_KILL, 0, 0);
    finish(&sender, 1);
  }
  state(&receiver, PROC_EXITING);
  clean(&receiver, LIFE_SIGNAL, SIGNAL_KILL);
  clean(&sender, self ? LIFE_SIGNAL : LIFE_EXIT, self ? SIGNAL_KILL : 42);
  closed(set);
}
static void
snapshot(void)
{
  static Actor receivers[32];
  Actor        sender, replacement, late;
  Setid        set;
  Handle       old;
  Sigstat      signal;
  u64          sequence, words[16], start, now;
  u32          i;
  CHECK(!setcreate(8192, 16384, 40, &set));
  create(set, 10, 20, &sender);
  isolate(&sender, 0);
  load(&sender, 1, 0);
  started(&sender);
  for (i = 0; i < 32; i++) {
    create(set, 10, 50, &receivers[i]);
    join(&receivers[i], &sender);
    load(&receivers[i], 0, 1);
    started(&receivers[i]);
    CHECK(!procstop(receivers[i].pid, SIGNAL_STOP));
  }
  sequence = begin(&sender, SIGNAL_SELECT_GROUP, sender.pid, SIGNAL_USR1, 0);
  CHECK(!clockmono(&start));
  do {
    step();
    trace(&sender, words);
    CHECK(words[7] != sequence && !sigstat(receivers[7].info.identity, &signal) &&
          !clockmono(&now) && now - start < UINT64_C(3000000000));
  } while (!signal.pending);
  for (i = 0; i < 8; i++)
    pending(&receivers[i], (u64) 1 << (SIGNAL_USR1 - 1));
  pending(&receivers[8], 0);
  word(&sender, 2, 99);
  word(&sender, 3, 0);
  old = receivers[31].pid;
  kill(&receivers[31]);
  create(set, 10, 50, &replacement);
  CHECK(replacement.pid != old && handleindex(replacement.pid) == handleindex(old));
  join(&replacement, &sender);
  load(&replacement, 0, 1);
  CHECK(!procstop(replacement.pid, SIGNAL_STOP));
  create(set, 10, 50, &late);
  join(&late, &sender);
  load(&late, 0, 1);
  CHECK(!procstop(late.pid, SIGNAL_STOP));
  completed(&sender, sequence, 0);
  metadata(&sender, &sender, 20, SIGNAL_USR1);
  for (i = 0; i < 31; i++) {
    pending(&receivers[i], (u64) 1 << (SIGNAL_USR1 - 1));
    kill(&receivers[i]);
  }
  pending(&replacement, 0);
  pending(&late, 0);
  kill(&replacement);
  kill(&late);
  finish(&sender, 1);
  clean(&sender, LIFE_EXIT, 42);
  closed(set);
}
void
sigsendtest(void)
{
  Actor  init;
  Setid  root;
  Set    budget, final;
  Pgstat before, after;
  Space  space;
  Kstack stack;
  u32    i;
  CHECK(!proctimerstart() && !setcreate(512, 1024, 1, &root) &&
        !procboot(root, 128, 256, LOCALS, &init.pid) && !procinfo(init.pid, &init.info));
  load(&init, 2, 1);
  state(&init, PROC_BLOCKED);
  CHECK(init.pid == 1 && !setstat(root, &budget));
  CHECK(!spacestat(init.info.space, &space) && !kstackstat(init.info.stack, &stack));
  putstr("INITRETAIN charged=");
  puthex(budget.charged);
  putstr(" root=");
  puthex(space.root);
  putstr(" stack_table=");
  puthex(stack.table);
  putstr(" stack_va=");
  puthex(KSTACK_BASE + (u64) init.info.stack.index * KSTACK_STRIDE + PAGE_SIZE);
  putstr("\n");
  for (i = 0; i < KSTACK_PAGES; i++) {
    putstr("INITSTACK index=");
    puthex(i);
    putstr(" pa=");
    puthex(stack.pa[i]);
    putstr("\n");
  }
  pgstat(&before);
  credentials(10, 99);
  credentials(99, 10);
  credentials(20, 99);
  credentials(99, 20);
  rootdenied();
  partial(0);
  partial(1);
  controls(0);
  controls(1);
  malformed();
  killing(0);
  killing(1);
  snapshot();
  pgstat(&after);
  pending(&init, 0);
  CHECK(!procinfo(init.pid, &init.info) && init.info.state == PROC_BLOCKED &&
        !setstat(root, &final) && final.charged == budget.charged && final.instances == 1 &&
        before.free == after.free && before.allocated == after.allocated && !pgcheck() &&
        !acctcheck() && !lifecheck() && !groupcheck());
  putstr("SIGSENDCHECK calls=");
  puthex(commands);
  putstr(" credentials=");
  puthex(4);
  putstr(" partial=");
  puthex(2);
  putstr(" root_denied=");
  puthex(1);
  putstr(" control=");
  puthex(2);
  putstr(" self_kill=");
  puthex(1);
  putstr(" snapshot=");
  puthex(32);
  putstr(" reused=");
  puthex(1);
  putstr(" late=");
  puthex(1);
  putstr(" init=");
  puthex(1);
  putstr(" init_charge=");
  puthex(final.charged);
  putstr(" charged=");
  puthex(0);
  putstr(" baseline=");
  puthex(before.allocated);
  putstr(" allocated=");
  puthex(after.allocated);
  putstr("\n");
}
