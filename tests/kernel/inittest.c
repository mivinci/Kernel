#include "ktest.h"
#include "load.h"
#include <abi/boot.h>
#include <diag.h>
#include <prep.h>
#include <rq.h>
typedef struct Initpacket Initpacket;
struct Initpacket
{
  Boot     header;
  Strref   arg;
  Resentry resources[4];
  char     name[8];
};
extern const u8  init_image[], init_image_end[];
extern const u64 init_trace_address;
#define REQUIRE(c)                                                                                 \
  do {                                                                                             \
    if (!(c)) {                                                                                    \
      putstr("INITFAIL line=");                                                                    \
      puthex(__LINE__);                                                                            \
      putstr("\n");                                                                                \
      panic("PID 1 adoption");                                                                     \
    }                                                                                              \
  } while (0)
void
inittest(void)
{
  Setid set;
  Set   baseline;
#if BOOT_CASE != 43
  Set account;
#endif
  Procinfo root;
#if BOOT_CASE != 43
  Procinfo children[3];
  u32      have[3];
#endif
  Handle       pid, again, create, control;
  Rtable      *table;
  Createpolicy policy;
  Initpacket   packet;
  Life         life;
  Kstack       stack;
  Space        space;
  u32          i;
#if BOOT_CASE != 43
  Pgstat pages;
  u64    trace[16];
  u32    turns = 0, old_result = 0;
#endif
  REQUIRE(!setcreate(16384, 32768, 16, &set));
  REQUIRE(procnew(set, 0, 512, 1024, LOCALS, &again) == -ERR_INVALID && !again);
  REQUIRE(!procboot(set, 512, 1024, LOCALS, &pid) && pid == 1 && !procinfo(pid, &root));
  REQUIRE(procboot(set, 512, 1024, LOCALS, &again) == -ERR_STATE && !again);
  REQUIRE(!lifestat(root.life, &life) && life.pid == 1 && !life.parent && life.group);
  table = proctable(pid);
  policy.set = set;
  policy.memlimit = 512;
  policy.exec_limit = 1024;
  policy.resources = LOCALS;
  policy.pending = 16;
  policy.rights = CREATE_USE;
  REQUIRE(!localcreate(table, &policy, &create));
  REQUIRE(!localcontrol(table, root.life, CONTROL_QUERY | CONTROL_CONNECT | (1UL << 63), &control));
  for (i = 0; i < sizeof(packet); i++)
    ((u8 *) &packet)[i] = 0;
  packet.header.magic = BOOT_MAGIC;
  packet.header.format = BOOT_FORMAT;
  packet.header.bytes = sizeof(packet);
  packet.header.argc = 1;
  packet.header.argv_off = offsetof(Initpacket, arg);
  packet.header.resource_count = 4;
  packet.header.resources_off = offsetof(Initpacket, resources);
  packet.header.user_lo = BOOT_USER_LO;
  packet.header.user_hi = BOOT_USER_HI;
  packet.arg.offset = offsetof(Initpacket, name);
#if BOOT_CASE == 43
  packet.arg.length = 6;
  packet.name[0] = 'f';
  packet.name[1] = 'a';
  packet.name[2] = 't';
  packet.name[3] = 'a';
  packet.name[4] = 'l';
#else
  packet.arg.length = 5;
  packet.name[0] = 'i';
  packet.name[1] = 'n';
  packet.name[2] = 'i';
  packet.name[3] = 't';
#endif
  packet.resources[0].tag = BOOT_SELF_SPACE;
  packet.resources[0].handle = testresource(table, LOCAL_SPACE);
  packet.resources[1].tag = BOOT_SELF_ALLOC;
  packet.resources[1].handle = testresource(table, LOCAL_ALLOC);
  packet.resources[2].tag = BOOT_CREATE;
  packet.resources[2].handle = create;
  packet.resources[3].tag = 257;
  packet.resources[3].handle = control;
  testload(pid, init_image, (u64) (init_image_end - init_image), &packet, sizeof(packet));
  REQUIRE(!setstat(set, &baseline) && baseline.instances == 1);
#if BOOT_CASE != 43
  for (i = 0; i < 3; i++)
    have[i] = 0;
#endif
  REQUIRE(!kstackstat(root.stack, &stack) && stack.state && stack.refs);
  REQUIRE(!spacestat(root.space, &space));
  putstr("INITRETAIN charged=");
  puthex(baseline.charged);
  putstr(" root=");
  puthex(space.root);
  putstr(" stack_table=");
  puthex(stack.table);
  putstr(" stack_va=");
  puthex(KSTACK_BASE + (u64) root.stack.index * KSTACK_STRIDE + PAGE_SIZE);
  putstr("\n");
  for (i = 0; i < KSTACK_PAGES; i++) {
    putstr("INITSTACK index=");
    puthex(i);
    putstr(" pa=");
    puthex(stack.pa[i]);
    putstr("\n");
  }
  REQUIRE(!procpublish(pid));
#if BOOT_CASE == 43
  /* PID 1 must never return from its real user exit. */
  REQUIRE(!procrunone());
  panic("init exit unexpectedly returned");
#else
  do {
    REQUIRE(!procrunone() && ++turns < 256);
    REQUIRE(!spaceread(root.space, init_trace_address, trace, sizeof(trace)));
    if (trace[1]) {
      putstr("INITRESULT stage=");
      puthex(trace[0]);
      putstr(" code=");
      puthex(trace[1]);
      putstr("\n");
      panic("user init assertion");
    }
    for (i = 0; i < 3; i++)
      if (trace[2 + i] && !have[i] && !procinfo((Handle) trace[2 + i], &children[i]))
        have[i] = 1;
    if (trace[0] == 3 && have[0] && have[1] && have[2]) {
      Life oldparent, ended, live;
      REQUIRE(!lifestat(children[0].life, &oldparent) && !lifestat(children[1].life, &ended) &&
              !lifestat(children[2].life, &live));
      if (oldparent.state == LIFE_LIVE && ended.state == LIFE_ENDED && live.state == LIFE_LIVE) {
        REQUIRE(ended.parent == oldparent.pid && live.parent == oldparent.pid && ended.waitref);
        old_result = 1;
      }
    }
    REQUIRE(procwork(8) >= 0 && prepwork(8) >= 0);
  } while (trace[0] != 99);
  for (i = 0; i < 256; i++)
    REQUIRE(procwork(32) >= 0 && prepwork(32) >= 0);
  REQUIRE(old_result);
  REQUIRE(trace[5] == 3 && trace[6] == 7 && trace[7] == 8 && trace[8] == 1 && trace[9] == 1 &&
          !trace[10] && trace[13] == 1 && trace[14] == 1 && trace[15] == 1);
  for (i = 0; i < 3; i++)
    REQUIRE(have[i] && lifestat(children[i].life, &life) == -ERR_STALE);
  REQUIRE(!lifestat(root.life, &life) && life.state == LIFE_LIVE && !life.waitref && !life.parent);
  REQUIRE(!setstat(set, &account) && account.instances == 1 &&
          account.charged == baseline.charged && account.refs == baseline.refs &&
          account.leases == baseline.leases);
  REQUIRE(!acctcheck() && !pgcheck() && !lifecheck());
  REQUIRE(!kstackstat(root.stack, &stack) && stack.state && stack.refs);
  pgstat(&pages);
  putstr("INITCHECK pid=");
  puthex(1);
  putstr(" collected=");
  puthex(trace[5]);
  putstr(" instances=");
  puthex(account.instances);
  putstr(" charged=");
  puthex(account.charged);
  putstr(" baseline=");
  puthex(baseline.charged);
  putstr(" allocated=");
  puthex(pages.allocated);
  putstr(" stack_table=");
  puthex(stack.table);
  putstr(" stack_va=");
  puthex(KSTACK_BASE + (u64) root.stack.index * KSTACK_STRIDE + PAGE_SIZE);
  putstr("\n");

#endif
}
