#include "ktest.h"
#include "load.h"
#include <abi/boot.h>
#include <signal.h>
#include <cow.h>
#include <vm.h>
#include <prep.h>
#include <diag.h>
typedef struct Framepacket Framepacket;
struct Framepacket
{
  Boot     header;
  Strref   arg;
  Resentry resources[2];
  char     name[8];
};
extern const u8  frame_image[], frame_image_end[];
extern const u64 frame_trace_address;
static Handle    forcedpid;
static u32       forcedarmed, forcedcount;
#define REQUIRE(c)                                                                                 \
  do {                                                                                             \
    if (!(c)) {                                                                                    \
      putstr("SIGFRAMEFAIL line=");                                                                \
      puthex(__LINE__);                                                                            \
      putstr("\n");                                                                                \
      panic("signal frames");                                                                      \
    }                                                                                              \
  } while (0)
void
faultframeenter(Hctx *context)
{
  Rtable  *table = proccurrent(context);
  Procinfo process;
  Siginfo  asynchronous, fault;
  Sigstat  state;
  u64      trace[32];
  u32      i;
  if (!forcedarmed || !table || table->owner != forcedpid)
    return;
  REQUIRE(!procinfo(forcedpid, &process) &&
          !spaceread(process.space, frame_trace_address, trace, sizeof(trace)));
  if (trace[0] != 1 || trace[1] != 11)
    return;
  forcedarmed = 0;
  for (i = 0; i < sizeof(fault); i++) {
    ((u8 *) &fault)[i] = 0;
    ((u8 *) &asynchronous)[i] = 0;
  }
  /* Private code=0 sentinels exercise transport only. Public FAULT reason
     values and actual hardware-fault production are not defined here. */
  asynchronous.signum = fault.signum = SIGNAL_SEGV;
  asynchronous.address = 0xb022;
  asynchronous.reason = 0xb2;
  asynchronous.status = 0xf002;
  fault.address = 0xa011;
  fault.reason = 0xa1;
  fault.status = 0xf001;
  REQUIRE(!sigqueue(process.identity, &asynchronous) &&
          !sigfault(process.identity, process.space, context, &fault) &&
          !sigstat(process.identity, &state) && state.depth == 1 &&
          state.pending == ((u64) 1 << (SIGNAL_SEGV - 1)));
  forcedcount++;
}
void
sigframetest(void)
{
  Setid               set;
  Set                 account;
  Handle              pid;
  Procinfo            process;
  Framepacket         packet;
  Siginfo             info;
  Sigstat             signal;
  Life                life;
  Liferesult          result;
  Pgstat              before, after;
  Acctid              shadow;
  Memid               stack, shadowmem;
  Mem                 stackstate;
  Rtable             *table;
  Hpte                cowentry;
  u64                 pa;
  const volatile u64 *peerpage;
  u64                 trace[32], previous = 0;
  u32                 mode, i, j, turns, first, nested, second;
  int                 error;
  pgstat(&before);
  REQUIRE(!setcreate(8192, 16384, 16, &set));
  for (mode = 0; mode < 12; mode++) {
    REQUIRE(!procnew(set, 1, 512, 1024, LOCALS, &pid) && !procinfo(pid, &process));
    for (i = 0; i < sizeof(packet); i++)
      ((u8 *) &packet)[i] = 0;
    packet.header.magic = BOOT_MAGIC;
    packet.header.format = BOOT_FORMAT;
    packet.header.bytes = sizeof(packet);
    packet.header.argc = 1;
    packet.header.argv_off = offsetof(Framepacket, arg);
    packet.header.resource_count = 2;
    packet.header.resources_off = offsetof(Framepacket, resources);
    packet.header.user_lo = BOOT_USER_LO;
    packet.header.user_hi = BOOT_USER_HI;
    packet.arg.offset = offsetof(Framepacket, name);
    packet.arg.length = 2;
    packet.name[0] = (char) ('0' + mode);
    packet.resources[0].tag = BOOT_SELF_SPACE;
    packet.resources[0].handle = testresource(proctable(pid), LOCAL_SPACE);
    packet.resources[1].tag = BOOT_SELF_ALLOC;
    packet.resources[1].handle = testresource(proctable(pid), LOCAL_ALLOC);
    testload(pid, frame_image, (u64) (frame_image_end - frame_image), &packet, sizeof(packet));
    shadow.generation = 0;
    stack.generation = 0;
    if (!mode) {
      table = proctable(pid);
      for (i = 0; i < table->count; i++) {
        if (table->slots[i].subtype != LOCAL_MEM || table->slots[i].length != 65536)
          continue;
        stack.generation = table->slots[i].objectgen;
        stack.index = table->slots[i].object;
        stack.reserved = 0;
        REQUIRE(!memstat(stack, &stackstate) && stackstate.state == MEM_DATA);
        break;
      }
      REQUIRE(stack.generation);
    }
    REQUIRE(!procpublish(pid));
    if (mode == 11) {
      forcedpid = pid;
      forcedarmed = 1;
    }
    if (!mode) {
      /* Freeze a published but not yet dispatched process. This does not relax
         native configure/publish validation or claim a fork-stack ABI. */
      REQUIRE(!acctcreate(set, 16, 16, &shadow) && !spaceclone(stack, shadow, &shadowmem));
    }
    first = nested = second = turns = 0;
    for (;;) {
      REQUIRE(!procrunone() && ++turns < 256);
      REQUIRE(!spaceread(process.space, frame_trace_address, trace, sizeof(trace)));
      if (trace[31]) {
        putstr("SIGFRAMEUSER mode=");
        puthex(mode);
        putstr(" code=");
        puthex(trace[31]);
        putstr("\n");
      }
      REQUIRE(!trace[31]);
      REQUIRE(!lifestat(process.life, &life));
      if (life.state == LIFE_ENDED)
        break;
      for (i = 0; i < sizeof(info); i++)
        ((u8 *) &info)[i] = 0;
      info.status = 0x12345678;
      if (mode < 11 && trace[0] == 1 && !first) {
        if (!mode)
          REQUIRE(spacelookup(process.space, 0x3ffffee000UL, &cowentry) &&
                  cowentry.prot == (VM_READ | VM_USER | VM_COW));
        info.signum = SIGNAL_USR1;
        REQUIRE(!procsigqueue(pid, &info));
        first = 1;
      }
      if (mode < 11 && trace[0] == 2 && !nested) {
        info.signum = mode == 9 ? SIGNAL_USR1 : SIGNAL_USR2;
        REQUIRE(!procsigqueue(pid, &info));
        nested = 1;
      }
      if (mode < 11 && trace[0] == 3 && !second) {
        info.signum = SIGNAL_USR1;
        REQUIRE(!procsigqueue(pid, &info));
        second = 1;
      }
      REQUIRE(procwork(8) >= 0 && prepwork(8) >= 0);
    }
    putstr("SIGFRAMECASE mode=");
    puthex(mode);
    putstr(" reason=");
    puthex(life.reason);
    putstr(" code=");
    puthex(life.code);
    putstr("\n");
    if (mode == 0 || mode == 1 || mode == 7 || mode == 9 || mode == 11) {
      REQUIRE(life.reason == LIFE_EXIT && life.code == 42 && (mode == 7 || trace[0] == 99));
      REQUIRE(trace[12] == (mode == 1 || mode == 9 || mode == 11 ? 2U : 1U));
      if (mode == 11)
        REQUIRE(!forcedarmed && forcedcount == 1 && trace[7] > trace[6]);
    } else if (mode == 8) {
      REQUIRE(life.reason == LIFE_SIGNAL && life.code == SIGNAL_USR1 && trace[12] == 1);
    } else {
      REQUIRE(life.reason == LIFE_FAULT && life.code == SIGNAL_SEGV &&
              life.fault.cause == SIGNAL_BAD_FRAME_CAUSE);
    }
    if (trace[6]) {
      REQUIRE(trace[6] > previous);
      previous = trace[6];
    }
    if (nested)
      REQUIRE(trace[7] > trace[6]);
    if (mode == 4)
      REQUIRE(second && trace[10] == trace[6]);
    if (!mode) {
      REQUIRE(spacelookup(process.space, 0x3ffffee000UL, &cowentry) &&
              cowentry.prot == (VM_READ | VM_WRITE | VM_USER));
      for (i = 0; i < 16; i++) {
        REQUIRE(!mempage(shadowmem, i, &pa));
        peerpage = kaddr(pa);
        for (j = 0; j < PAGE_SIZE / sizeof(u64); j++)
          REQUIRE(!peerpage[j]);
      }
    }
    REQUIRE(!sigstat(process.identity, &signal) && !signal.depth && !sigcheck());
    while ((error = procclean(pid)) > 0) {
    }
    REQUIRE(!error && !lifecollect(process.life, 1, &result));
    if (shadow.generation)
      REQUIRE(!memfree(shadowmem) && !acctclose(shadow) && !acctunbind(shadow));
    for (i = 0; i < 128; i++)
      REQUIRE(procwork(32) >= 0 && prepwork(32) >= 0);
    REQUIRE(!setstat(set, &account) && !account.charged && !account.instances);
  }
  REQUIRE(!setclose(set));
  pgstat(&after);
  REQUIRE(before.allocated == after.allocated && before.free == after.free && !pgcheck() &&
          !acctcheck() && !sigcheck() && !lifecheck() && !groupcheck());
  putstr("SIGFRAMECHECK cases=");
  puthex(12);
  putstr(" normal=");
  puthex(5);
  putstr(" invalid=");
  puthex(6);
  putstr(" terminated=");
  puthex(1);
  putstr(" cow_stack=");
  puthex(1);
  putstr(" forced=");
  puthex(forcedcount);
  putstr(" charged=");
  puthex(0);
  putstr(" allocated=");
  puthex(after.allocated);
  putstr("\n");
}
