#include "ktest.h"
#include <proc.h>
#include <rq.h>
#include <abi/boot.h>
#include <diag.h>
extern const u8 term_begin[], term_end[], term_recv[], term_call[], term_ready[];
extern const u8 term_finish[], term_normal[], term_busy[];
#define REQUIRE(c)                                                                                 \
  do {                                                                                             \
    if (!(c)) {                                                                                    \
      putstr("TERMFAIL line=");                                                                    \
      puthex(__LINE__);                                                                            \
      putstr("\n");                                                                                \
      panic("native termination");                                                                 \
    }                                                                                              \
  } while (0)
static Handle
resource(Rtable *t, u32 kind)
{
  u32 i;
  for (i = 0; i < t->count; i++)
    if (t->slots[i].subtype == kind)
      return handlepack(HANDLE_RESOURCE, t->slots[i].generation, i);
  panic("termination boot resource");
}
static void
load(Handle pid, const Procinfo *self, const Procinfo info[9], u32 role)
{
  Rtable  *t = proctable(pid);
  Handle   allocator = resource(t, LOCAL_ALLOC), space = resource(t, LOCAL_SPACE);
  Handle   code, stack, boot, data;
  Allocarg args;
  Config   c;
  Boot     header;
  u64      resources[8], config[17], entry = 0x10000;
  u32      i;
  args.bytes = 4 * PAGE_SIZE;
  args.kind = ALLOC_CODE;
  args.align = args.minaddr = args.maxaddr = args.reserved = 0;
  REQUIRE(!localalloc(t, allocator, &args, &code));
  REQUIRE((u64) (term_end - term_begin) <= args.bytes);
  REQUIRE(!localwrite(t, code, 0, term_begin, (u64) (term_end - term_begin)) &&
          !localseal(t, code));
  args.bytes = 65536;
  args.kind = ALLOC_DATA;
  REQUIRE(!localalloc(t, allocator, &args, &stack));
  args.bytes = PAGE_SIZE;
  REQUIRE(!localalloc(t, allocator, &args, &boot));
  REQUIRE(!localalloc(t, allocator, &args, &data));
  for (i = 0; i < sizeof(header); i++)
    ((u8 *) &header)[i] = 0;
  for (i = 0; i < 8; i++)
    resources[i] = 0;
  header.magic = BOOT_MAGIC;
  header.format = 1;
  header.bytes = sizeof(header) + sizeof(resources);
  header.resource_count = 2;
  header.resources_off = sizeof(header);
  header.user_lo = entry;
  header.user_hi = 1UL << 38;
  resources[0] = 1;
  resources[1] = (u64) space;
  resources[4] = 2;
  resources[5] = (u64) allocator;
  REQUIRE(!localwrite(t, boot, 0, &header, sizeof(header)) &&
          !localwrite(t, boot, sizeof(header), resources, sizeof(resources)));
  REQUIRE(!localmap(t, space, code, 0, entry, 4 * PAGE_SIZE, VM_READ | VM_EXEC));
  REQUIRE(!localmap(t, space, stack, 0, 0x3ffffe0000UL, 65536, VM_READ | VM_WRITE));
  REQUIRE(!localmap(t, space, boot, 0, 0x3fff000000UL, PAGE_SIZE, VM_READ));
  REQUIRE(!localmap(t, space, data, 0, 0x40000, PAGE_SIZE, VM_READ | VM_WRITE));
  for (i = 0; i < 17; i++)
    config[i] = 0;
  REQUIRE(!localcontrol(t, self->life, CONTROL_TERMINATE, (Handle *) &config[0]));
  if (!role) {
    for (i = 1; i <= 7; i++)
      REQUIRE(!localcontrol(t, info[i].life, CONTROL_TERMINATE | CONTROL_CONNECT,
                            (Handle *) &config[i]));
    REQUIRE(!localcontrol(t, info[1].life, (CONTROL_RIGHTS & ~CONTROL_TERMINATE) | LOCAL_GRANT,
                          (Handle *) &config[8]));
    config[9] = (u64) info[1].pid;
    REQUIRE(
        !localendpoint(t, info[1].identity, LOCAL_CALL, LOCAL_USE, 0, 0, (Handle *) &config[10]));
    for (i = 0; i < 3; i++)
      config[13 + i] = (u64) info[3 + i].pid;
  }
  REQUIRE(!localendpoint(t, info[0].identity, LOCAL_NOTIFY, LOCAL_USE, 31UL << 32, 0,
                         (Handle *) &config[11]));
  REQUIRE(!localendpoint(t, info[0].identity, LOCAL_CALL, LOCAL_USE, 0, 0, (Handle *) &config[12]));
  config[16] = (u64) pid;
  REQUIRE(!localwrite(t, data, 0, config, sizeof(config)));
  c.entry = entry;
  if (role == 2)
    c.entry += (u64) (term_recv - term_begin);
  else if (role == 3)
    c.entry += (u64) (term_call - term_begin);
  else if (role == 4)
    c.entry += (u64) (term_ready - term_begin);
  else if (role == 5)
    c.entry += (u64) (term_finish - term_begin);
  else if (role == 6)
    c.entry += (u64) (term_normal - term_begin);
  else if (role == 8)
    c.entry += (u64) (term_busy - term_begin);
  c.stack = 0x3fffff0000UL;
  c.boot = 0x3fff000000UL;
  c.bootsz = header.bytes;
  c.sigmask = c.sigdefault = 0;
  REQUIRE(!procconfigure(pid, &c, 0));
}
static void
clean(Handle pid)
{
  int error;
  while ((error = procclean(pid)) > 0) {
  }
  REQUIRE(!error);
}
static void
collect(Handle pid, Lifeid identity, u32 reason, u32 code)
{
  Liferesult result;
  clean(pid);
  REQUIRE(!lifecollect(identity, 1, &result) && result.reason == reason && result.code == code &&
          result.sequence == 2);
}
void
terminatetest(void)
{
  Setid    set;
  Set      group;
  Handle   pids[9];
  Procinfo info[9], state;
  Life     life;
  Acct     account;
  Rqid     requests[3], candidate;
  Rqinfo   request;
  Pgstat   before, after;
  u32      i, j, index, captured = 0, receiver = 0, turns = 0, reused = 0, ended, live = 0;
  pgstat(&before);
  REQUIRE(!setcreate(4096, 8192, 16, &set));
  for (i = 0; i < 8; i++)
    REQUIRE(!procnew(set, 1, 256, 512, LOCALS, &pids[i]) && !procinfo(pids[i], &info[i]));
  for (i = 0; i < 7; i++) {
    load(pids[i], &info[i], info, i);
    REQUIRE(!procpublish(pids[i]));
  }
  for (;;) {
    REQUIRE(!procrunone() && ++turns < 10000);
    if (!reused) {
      if (!procinfo(pids[2], &state) && state.state == PROC_BLOCKED)
        receiver = 1;
      candidate.generation = 1;
      candidate.reserved = 0;
      for (index = 0; captured != 7 && index < REQUESTS; index++) {
        candidate.index = index;
        if (rqstat(candidate, &request))
          continue;
        for (j = 0; j < 3; j++)
          if (!(captured & (1U << j)) && request.caller == pids[3 + j]) {
            REQUIRE(request.refs == 1 && request.state == RQ_QUEUED);
            requests[j] = candidate;
            captured |= 1U << j;
          }
      }
      ended = 0;
      for (i = 1; i <= 6; i++) {
        REQUIRE(!lifestat(info[i].life, &life));
        if (life.state == LIFE_ENDED) {
          if (life.reason != (i == 6 ? LIFE_EXIT : LIFE_SIGNAL) ||
              life.code != (i == 6 ? 23U : 9U) || life.sequence != 2) {
            putstr("TERMRESULT actor=");
            puthex(i);
            putstr(" code=");
            puthex(life.code);
            putstr("\n");
            panic("terminated user result");
          }
          ended++;
        }
      }
      if (ended == 6) {
        REQUIRE(receiver && captured == 7);
        for (j = 0; j < 3; j++) {
          if (j == 1)
            REQUIRE(rqstat(requests[j], &request) == -ERR_STALE);
          else {
            REQUIRE(!rqstat(requests[j], &request) && request.state == RQ_TERMINAL &&
                    !request.refs && !(request.flags & RQ_FINISHED) &&
                    (request.flags & (RQ_CONSUMED | RQ_RELEASED)) == (RQ_CONSUMED | RQ_RELEASED));
          }
        }
        for (i = 1; i <= 6; i++) {
          REQUIRE(!acctstat(info[i].acct, &account) && account.state == AC_CLOSING &&
                  acctreserve(info[i].acct, 1) == AC_BAD);
          collect(pids[i], info[i].life, i == 6 ? LIFE_EXIT : LIFE_SIGNAL, i == 6 ? 23 : 9);
        }
        REQUIRE(!procdestroy(pids[7]));
        clean(pids[7]);
        REQUIRE(!procnew(set, 1, 256, 512, LOCALS, &pids[8]) && !procinfo(pids[8], &info[8]));
        REQUIRE(handleindex(pids[1]) == handleindex(pids[8]) && pids[1] != pids[8]);
        load(pids[8], &info[8], info, 8);
        REQUIRE(!procpublish(pids[8]) && !proctimerstart());
        reused = 1;
      }
    }
    REQUIRE(!lifestat(info[0].life, &life));
    if (life.state == LIFE_ENDED) {
      if (life.reason != LIFE_EXIT || life.code != 42) {
        putstr("TERMMANAGER code=");
        puthex(life.code);
        putstr("\n");
        panic("termination manager result");
      }
      REQUIRE(reused && !lifestat(info[8].life, &life));
      if (!live) {
        REQUIRE(life.state == LIFE_LIVE);
        live = 1;
      }
      if (life.state == LIFE_ENDED) {
        REQUIRE(life.reason == LIFE_SIGNAL && life.code == 9);
        break;
      }
    }
  }
  REQUIRE(live && turns >= 10);
  for (j = 0; j < 3; j++)
    REQUIRE(rqstat(requests[j], &request) == -ERR_STALE);
  collect(pids[0], info[0].life, LIFE_EXIT, 42);
  collect(pids[8], info[8].life, LIFE_SIGNAL, 9);
  REQUIRE(!setstat(set, &group) && !group.charged && !group.instances && !setclose(set));
  pgstat(&after);
  REQUIRE(before.allocated == after.allocated && before.free == after.free && !pgcheck() &&
          !acctcheck());
  putstr("TERMCHECK turns=");
  puthex(turns);
  putstr(" killed=");
  puthex(6);
  putstr(" draining=");
  puthex(2);
  putstr(" reused=");
  puthex(reused);
  putstr(" refs=");
  puthex(0);
  putstr(" charged=");
  puthex(group.charged);
  putstr(" allocated=");
  puthex(after.allocated);
  putstr("\n");
}
