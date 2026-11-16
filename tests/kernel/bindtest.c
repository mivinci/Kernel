#include "ktest.h"
#include "load.h"
#include "chip.h"
#include <irq.h>
#include <abi/boot.h>
#include <proc.h>
#include <prep.h>
#include <diag.h>
typedef struct Irqpacket Irqpacket;
struct Irqpacket
{
  Boot     header;
  Strref   arg;
  Resentry resources[6];
  char     name[8];
};
extern const u8  irq_image[], irq_image_end[];
extern const u64 irq_trace_address;
#define REQUIRE(c)                                                                                 \
  do {                                                                                             \
    if (!(c)) {                                                                                    \
      putstr("IRQBINDFAIL line=");                                                                 \
      puthex(__LINE__);                                                                            \
      putstr("\n");                                                                                \
      panic("IRQ binding");                                                                        \
    }                                                                                              \
  } while (0)
static void
quiet(u32 number)
{
  u64 token;
  u32 i;
  chipend();
  hal_irq_mask(number);
  hal_irq_unmask(number);
  for (i = 0; i < 4; i++) {
    hal_timer_ack();
    token = hal_irq_claim();
    if (!token)
      break;
    REQUIRE(hal_irq_number(token) == number);
    hal_irq_mask(number);
    hal_irq_complete(token);
    hal_irq_unmask(number);
  }
  hal_irq_mask(number);
  REQUIRE(i < 4);
}
static void
busyload(Handle pid)
{
  Irqpacket packet;
  Rtable   *table = proctable(pid);
  u32       i;
  for (i = 0; i < sizeof(packet); i++)
    ((u8 *) &packet)[i] = 0;
  packet.header.magic = BOOT_MAGIC;
  packet.header.format = BOOT_FORMAT;
  packet.header.bytes = sizeof(packet);
  packet.header.argc = 1;
  packet.header.argv_off = offsetof(Irqpacket, arg);
  packet.header.resource_count = 2;
  packet.header.resources_off = offsetof(Irqpacket, resources);
  packet.header.user_lo = BOOT_USER_LO;
  packet.header.user_hi = BOOT_USER_HI;
  packet.arg.offset = offsetof(Irqpacket, name);
  packet.arg.length = 2;
  packet.name[0] = '7';
  packet.resources[0].tag = BOOT_SELF_SPACE;
  packet.resources[0].handle = testresource(table, LOCAL_SPACE);
  packet.resources[1].tag = BOOT_SELF_ALLOC;
  packet.resources[1].handle = testresource(table, LOCAL_ALLOC);
  testload(pid, irq_image, (u64) (irq_image_end - irq_image), &packet, sizeof(packet));
}
void
bindtest(const Dtinfo *hardware)
{
  Setid      set;
  Set        account;
  Handle     pid, binding, control, weak, kill, stale = 0, busy, busy_kill;
  Rtable    *table, *busy_table;
  Procinfo   process, current, busy_info;
  Irqpacket  packet;
  Irqid      identity;
  Irqstat    state;
  Life       life;
  Liferesult result;
  Pgstat     before, after;
  u64        trace[32];
  u32        number = 0, mode, i, turns, received = 0, stale_checks = 0, early = 0, busy_proofs = 0;
  int        error;
  for (i = 0; i < hardware->ndev; i++)
    if (hardware->dev[i].kind == DT_UART16550 || hardware->dev[i].kind == DT_PL011)
      number = hardware->dev[i].irq[0].number;
  REQUIRE(number);
  chipbegin();
  quiet(number);
  pgstat(&before);
  REQUIRE(!setcreate(8192, 16384, 16, &set));
  for (mode = 0; mode < 7; mode++) {
    if (mode == 4)
      REQUIRE(!proctimerstart());
    REQUIRE(!procnew(set, 1, 512, 1024, LOCALS, &pid) && !procinfo(pid, &process));
    table = proctable(pid);
    REQUIRE(!procirqinstall(pid, number, 16, &binding));
    identity.generation = table->slots[handleindex(binding)].objectgen;
    identity.index = table->slots[handleindex(binding)].object;
    identity.reserved = 0;
    REQUIRE(!irqstat(identity, &state) && state.state == IRQ_DISABLED &&
            localirqack(table, binding, process.identity) == -ERR_STATE);
    REQUIRE(!localcontrol(table, process.life, CONTROL_IRQ, &control) &&
            !localcontrol(table, process.life, CONTROL_QUERY, &weak) &&
            !localcontrol(table, process.life, CONTROL_TERMINATE, &kill));
    for (i = 0; i < sizeof(packet); i++)
      ((u8 *) &packet)[i] = 0;
    packet.header.magic = BOOT_MAGIC;
    packet.header.format = BOOT_FORMAT;
    packet.header.bytes = sizeof(packet);
    packet.header.argc = 1;
    packet.header.argv_off = offsetof(Irqpacket, arg);
    packet.header.resource_count = stale ? 6 : 5;
    packet.header.resources_off = offsetof(Irqpacket, resources);
    packet.header.user_lo = BOOT_USER_LO;
    packet.header.user_hi = BOOT_USER_HI;
    packet.arg.offset = offsetof(Irqpacket, name);
    packet.arg.length = 2;
    packet.name[0] = (char) ('0' + mode);
    packet.resources[0].tag = BOOT_SELF_SPACE;
    packet.resources[0].handle = testresource(table, LOCAL_SPACE);
    packet.resources[1].tag = BOOT_SELF_ALLOC;
    packet.resources[1].handle = testresource(table, LOCAL_ALLOC);
    packet.resources[2].tag = 257;
    packet.resources[2].handle = binding;
    packet.resources[3].tag = 258;
    packet.resources[3].handle = control;
    packet.resources[4].tag = 259;
    packet.resources[4].handle = weak;
    packet.resources[5].tag = 260;
    packet.resources[5].handle = stale;
    testload(pid, irq_image, (u64) (irq_image_end - irq_image), &packet, sizeof(packet));
    if (mode == 6) {
      REQUIRE(!imageend(process.identity, IMAGE_ABORT) && !lifecancel(process.life));
      REQUIRE(!irqstat(identity, &state) && state.state == IRQ_REVOKED);
      goto clean;
    }
    REQUIRE(!procpublish(pid) && !procrunone());
    REQUIRE(!spaceread(process.space, irq_trace_address, trace, sizeof(trace)) && trace[0] == 1 &&
            !trace[31]);
    REQUIRE(!irqstat(identity, &state) && state.state == IRQ_ARMED);
    if (mode == 3) {
      REQUIRE(!procnew(set, 1, 512, 1024, LOCALS, &busy) && !procinfo(busy, &busy_info));
      busy_table = proctable(busy);
      REQUIRE(!localcontrol(busy_table, busy_info.life, CONTROL_TERMINATE, &busy_kill));
      busyload(busy);
      REQUIRE(!procpublish(busy));
    }
    if (mode >= 3) {
      REQUIRE(!procrunone() && !procinfo(pid, &current) && current.state == PROC_BLOCKED);
      REQUIRE(!spaceread(process.space, irq_trace_address, trace, sizeof(trace)) && trace[0] == 4 &&
              !trace[31]);
    }
    chipbegin();
    if (mode == 1 || mode == 2) {
      hal_idle();
      REQUIRE(!irqstat(identity, &state) && state.state == IRQ_DELIVERED);
    } else if (mode == 3) {
      REQUIRE(!procrunone() && !procticks() && !prockernelticks() && !irqstat(identity, &state) &&
              state.state == IRQ_DELIVERED);
      REQUIRE(!procinfo(pid, &current) && current.state == PROC_READY &&
              !procinfo(busy, &current) && current.state == PROC_READY);
      REQUIRE(!procterminate(busy_table, busy_kill));
      while ((error = procclean(busy)) > 0) {
      }
      REQUIRE(!error && !lifecollect(busy_info.life, 1, &result) && result.reason == LIFE_SIGNAL &&
              result.code == 9);
      REQUIRE(!irqstat(identity, &state) && state.state == IRQ_DELIVERED);
      busy_proofs++;
    } else if (mode >= 3) {
      for (turns = 0; turns < 64; turns++) {
        REQUIRE(procidle() >= 0 && !irqstat(identity, &state));
        if (state.state == IRQ_DELIVERED)
          break;
      }
      REQUIRE(turns < 64 && !procinfo(pid, &current) && current.state == PROC_READY);
    }
    if (mode == 5) {
      quiet(number);
      REQUIRE(!procirqunbind(table, control, binding));
      REQUIRE(!procrunone() && !procinfo(pid, &current) && current.state == PROC_BLOCKED);
      REQUIRE(!spaceread(process.space, irq_trace_address, trace, sizeof(trace)) && trace[0] == 4 &&
              !trace[2] && !trace[31]);
      stale_checks += (u32) trace[6];
      REQUIRE(!procterminate(table, kill));
    } else {
      REQUIRE(!procrunone());
      REQUIRE(!spaceread(process.space, irq_trace_address, trace, sizeof(trace)));
      if (trace[31]) {
        putstr("IRQUSER mode=");
        puthex(mode);
        putstr(" code=");
        puthex(trace[31]);
        putstr("\n");
      }
      REQUIRE(!trace[31] && trace[0] == 2 && trace[2] == 1);
      REQUIRE(!irqstat(identity, &state) && state.state == IRQ_DELIVERED);
      quiet(number);
      REQUIRE(!procrunone());
      REQUIRE(!spaceread(process.space, irq_trace_address, trace, sizeof(trace)) &&
              trace[0] == 99 && trace[7] && !trace[31]);
      received += (u32) trace[2];
      stale_checks += (u32) trace[6];
      early += (u32) trace[3];
    }
    REQUIRE(!lifestat(process.life, &life) && life.state == LIFE_ENDED &&
            life.reason == (mode == 5 ? LIFE_SIGNAL : LIFE_EXIT) &&
            life.code == (mode == 5 ? 9 : 42));
    if (mode == 2)
      REQUIRE(!irqstat(identity, &state) && state.state == IRQ_REVOKED);
    else
      REQUIRE(irqstat(identity, &state) == -ERR_STALE);
  clean:
    stale = binding;
    while ((error = procclean(pid)) > 0) {
    }
    REQUIRE(!error);
    if (mode != 6)
      REQUIRE(!lifecollect(process.life, 1, &result));
    for (i = 0; i < 128; i++)
      REQUIRE(procwork(32) >= 0 && prepwork(32) >= 0);
    REQUIRE(!setstat(set, &account) && !account.charged && !account.instances && !irqcheck() &&
            irqstat(identity, &state) == -ERR_STALE);
    quiet(number);
  }
  REQUIRE(!setclose(set));
  pgstat(&after);
  REQUIRE(before.allocated == after.allocated && before.free == after.free && !pgcheck() &&
          !acctcheck() && !lifecheck() && !groupcheck());
  putstr("IRQBINDCHECK cases=");
  puthex(7);
  putstr(" received=");
  puthex(received);
  putstr(" stale=");
  puthex(stale_checks);
  putstr(" early=");
  puthex(early);
  putstr(" revoked=");
  puthex(2);
  putstr(" busy=");
  puthex(busy_proofs);
  putstr(" charged=");
  puthex(0);
  putstr(" allocated=");
  puthex(after.allocated);
  putstr("\n");
}
