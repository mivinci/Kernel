#include "ktest.h"
#include <proc.h>
#include <abi/boot.h>
#include <diag.h>
extern const u8 control_begin[], control_end[], control_peer[], control_replacement[];
#define REQUIRE(c)                                                                                 \
  do {                                                                                             \
    if (!(c)) {                                                                                    \
      putstr("CONTROLFAIL line=");                                                                 \
      puthex(__LINE__);                                                                            \
      putstr("\n");                                                                                \
      panic("native control");                                                                     \
    }                                                                                              \
  } while (0)
static Handle
resource(Rtable *t, u32 kind)
{
  u32 i;
  for (i = 0; i < t->count; i++)
    if (t->slots[i].subtype == kind)
      return handlepack(HANDLE_RESOURCE, t->slots[i].generation, i);
  panic("control boot resource");
}
static void
load(Handle pid, const Procinfo *self, const Procinfo *peer, u32 role)
{
  Rtable  *t = proctable(pid);
  Handle   allocator = resource(t, LOCAL_ALLOC), space = resource(t, LOCAL_SPACE);
  Handle   code, stack, boot, data;
  Allocarg args;
  Acct     account;
  Config   c;
  Boot     header;
  u64      resources[8], config[17], entry = 0x10000;
  u32      i;
  args.bytes = 8 * PAGE_SIZE;
  args.kind = ALLOC_CODE;
  args.align = args.minaddr = args.maxaddr = args.reserved = 0;
  REQUIRE(!localalloc(t, allocator, &args, &code));
  REQUIRE((u64) (control_end - control_begin) <= args.bytes);
  REQUIRE(!localwrite(t, code, 0, control_begin, (u64) (control_end - control_begin)) &&
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
  REQUIRE(!localmap(t, space, code, 0, entry, 8 * PAGE_SIZE, VM_READ | VM_EXEC));
  REQUIRE(!localmap(t, space, stack, 0, 0x3ffffe0000UL, 65536, VM_READ | VM_WRITE));
  REQUIRE(!localmap(t, space, boot, 0, 0x3fff000000UL, PAGE_SIZE, VM_READ));
  REQUIRE(!localmap(t, space, data, 0, 0x40000, PAGE_SIZE, VM_READ | VM_WRITE));
  REQUIRE(!localcontrol(t, self->life, CONTROL_INSTALL | CONTROL_REVOKE | CONTROL_CONNECT,
                        (Handle *) &config[0]));
  REQUIRE(!localcontrol(t, peer->life, CONTROL_INSTALL | CONTROL_REVOKE | CONTROL_CONNECT,
                        (Handle *) &config[1]));
  REQUIRE(!localcontrol(t, peer->life, CONTROL_QUERY | CONTROL_CONNECT | LOCAL_GRANT,
                        (Handle *) &config[2]));
  REQUIRE(!localcontrol(t, self->life, CONTROL_CONNECT | LOCAL_GRANT, (Handle *) &config[3]));
  REQUIRE(!localendpoint(t, self->identity, LOCAL_CALL, LOCAL_USE, 0, 0, (Handle *) &config[4]));
  config[5] = (u64) space;
  config[6] = (u64) data;
  config[7] = (u64) peer->pid;
  config[8] = (u64) pid;
  REQUIRE(!localcontrol(t, peer->life, CONTROL_INSTALL, (Handle *) &config[9]));
  REQUIRE(!localcontrol(t, peer->life, CONTROL_REVOKE | LOCAL_GRANT, (Handle *) &config[10]));
  REQUIRE(!localendpoint(t, peer->identity, LOCAL_CALL, LOCAL_USE, 0, 0, (Handle *) &config[11]));
  REQUIRE(!localendpoint(t, peer->identity, LOCAL_NOTIFY, LOCAL_USE, 7UL << 32, 0,
                         (Handle *) &config[12]));
  REQUIRE(!localcontrol(t, self->life, CONTROL_BUDGET, (Handle *) &config[13]));
  REQUIRE(!localcontrol(t, peer->life, CONTROL_BUDGET, (Handle *) &config[14]));
  REQUIRE(!acctstat(self->acct, &account));
  config[15] = account.used + account.reserved;
  config[16] = (u64) allocator;
  REQUIRE(!localwrite(t, data, 0, config, sizeof(config)));
  c.entry = entry + (role == 2 ? (u64) (control_replacement - control_begin)
                     : role    ? (u64) (control_peer - control_begin)
                               : 0);
  c.stack = 0x3fffff0000UL;
  c.boot = 0x3fff000000UL;
  c.bootsz = header.bytes;
  c.sigmask = c.sigdefault = 0;
  REQUIRE(!procconfigure(pid, &c, 0));
}
static void
clean(Handle pid, Lifeid identity)
{
  Liferesult result;
  int        error;
  while ((error = procclean(pid)) > 0) {
  }
  REQUIRE(!error && !lifecollect(identity, 1, &result) && result.code == 42);
}
void
controltest(void)
{
  Setid    set;
  Set      group;
  Handle   pids[3];
  Procinfo info[3];
  Life     life;
  Pgstat   before, after;
  u32      i, turns = 0, reused = 0, ended;
  pgstat(&before);
  REQUIRE(!setcreate(4096, 8192, 8, &set));
  for (i = 0; i < 2; i++)
    REQUIRE(!procnew(set, 1, 256, 512, LOCALS, &pids[i]) && !procinfo(pids[i], &info[i]));
  for (i = 0; i < 2; i++)
    load(pids[i], &info[i], &info[1 - i], i);
  REQUIRE(!procpublish(pids[0]) && !procpublish(pids[1]));
  for (;;) {
    REQUIRE(!procrunone() && ++turns < 100);
    if (!reused) {
      REQUIRE(!lifestat(info[1].life, &life));
      if (life.state == LIFE_ENDED) {
        REQUIRE(life.reason == LIFE_EXIT && life.code == 42);
        clean(pids[1], info[1].life);
        REQUIRE(!procnew(set, 1, 256, 512, LOCALS, &pids[2]) && !procinfo(pids[2], &info[2]));
        REQUIRE(handleindex(pids[1]) == handleindex(pids[2]) && pids[1] != pids[2]);
        load(pids[2], &info[2], &info[0], 2);
        REQUIRE(!procpublish(pids[2]));
        reused = 1;
      }
    }
    ended = 0;
    for (i = 0; i < 3; i += 2) {
      if (i == 2 && !reused)
        continue;
      REQUIRE(!lifestat(info[i].life, &life));
      if (life.state == LIFE_ENDED) {
        if (life.reason != LIFE_EXIT || life.code != 42) {
          putstr("CONTROLRESULT actor=");
          puthex(i);
          putstr(" code=");
          puthex(life.code);
          putstr("\n");
          panic("control user result");
        }
        ended++;
      }
    }
    if (ended == 2)
      break;
  }
  REQUIRE(reused && turns >= 6);
  clean(pids[0], info[0].life);
  clean(pids[2], info[2].life);
  REQUIRE(!setstat(set, &group) && !group.charged && !group.instances && !setclose(set));
  pgstat(&after);
  REQUIRE(before.allocated == after.allocated && before.free == after.free && !pgcheck() &&
          !acctcheck());
  putstr("CONTROLCHECK turns=");
  puthex(turns);
  putstr(" reused=");
  puthex(reused);
  putstr(" exits=");
  puthex(3);
  putstr(" charged=");
  puthex(group.charged);
  putstr(" allocated=");
  puthex(after.allocated);
  putstr("\n");
}
