#include "ktest.h"
#include <proc.h>
#include <abi/boot.h>
#include <diag.h>
extern const u8 cred_begin[], cred_end[], cred_server[], cred_replacement[];
#define REQUIRE(c)                                                                                 \
  do {                                                                                             \
    if (!(c)) {                                                                                    \
      putstr("CREDFAIL line=");                                                                    \
      puthex(__LINE__);                                                                            \
      putstr("\n");                                                                                \
      panic("native credentials");                                                                 \
    }                                                                                              \
  } while (0)
static Handle
resource(Rtable *t, u32 kind)
{
  u32 i;
  for (i = 0; i < t->count; i++)
    if (t->slots[i].subtype == kind)
      return handlepack(HANDLE_RESOURCE, t->slots[i].generation, i);
  panic("credential boot resource");
}
static void
load(Handle pid, const Procinfo *peer, Handle child, u32 role)
{
  Rtable  *t = proctable(pid);
  Handle   allocator = resource(t, LOCAL_ALLOC), space = resource(t, LOCAL_SPACE);
  Handle   code, stack, boot, data;
  Allocarg args;
  Config   c;
  Boot     header;
  u64      resources[8], config[10], entry = 0x10000;
  u32      i;
  args.bytes = 4 * PAGE_SIZE;
  args.kind = ALLOC_CODE;
  args.align = args.minaddr = args.maxaddr = args.reserved = 0;
  REQUIRE(!localalloc(t, allocator, &args, &code));
  REQUIRE((u64) (cred_end - cred_begin) <= args.bytes);
  REQUIRE(!localwrite(t, code, 0, cred_begin, (u64) (cred_end - cred_begin)) &&
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
  for (i = 0; i < 10; i++)
    config[i] = 0;
  REQUIRE(!localendpoint(t, peer->identity, LOCAL_CALL, LOCAL_USE, 0, 0, (Handle *) &config[0]));
  REQUIRE(!localcontrol(t, peer->life, CONTROL_QUERY, (Handle *) &config[1]));
  REQUIRE(!localcontrol(t, peer->life, CONTROL_CONNECT | LOCAL_GRANT, (Handle *) &config[2]));
  config[3] = (u64) child;
  config[4] = (u64) peer->pid;
  config[5] = (u64) pid;
  REQUIRE(!localendpoint(t, peer->identity, LOCAL_NOTIFY, LOCAL_USE, 3UL << 32, 0,
                         (Handle *) &config[6]));
  config[9] = 1;
  REQUIRE(!localwrite(t, data, 0, config, sizeof(config)));
  c.entry = entry + (role == 2 ? (u64) (cred_replacement - cred_begin)
                     : role    ? (u64) (cred_server - cred_begin)
                               : 0);
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
collect(Handle pid, Lifeid identity)
{
  Liferesult result;
  clean(pid);
  REQUIRE(!lifecollect(identity, 1, &result) && result.reason == LIFE_EXIT && result.code == 42);
}
void
credtest(void)
{
  Setid    set;
  Set      group;
  Handle   pids[4], construct;
  Procinfo info[4];
  Life     life;
  Cred     cred;
  Pgstat   before, after;
  u64      marker, discovery[3];
  u32      i, turns = 0, reused = 0, proofgone = 0, ended;
  pgstat(&before);
  REQUIRE(!setcreate(4096, 8192, 8, &set));
  for (i = 0; i < 3; i++) {
    REQUIRE(!procnew(set, i == 2 ? pids[0] : 1, 256, 512, LOCALS, &pids[i]) &&
            !procinfo(pids[i], &info[i]));
    cred.ruid = i == 0 ? 10 : i == 1 ? 50 : 110;
    cred.euid = cred.ruid + 10;
    cred.rgid = cred.ruid + 20;
    cred.egid = cred.ruid + 30;
    REQUIRE(!lifecredentials(info[i].life, &cred));
  }
  load(pids[0], &info[1], pids[2], 0);
  load(pids[1], &info[0], pids[2], 1);
  REQUIRE(!procprepare(proctable(pids[0]), pids[2], CONSTRUCT_CONFIG, &construct));
  discovery[0] = (u64) construct;
  discovery[1] = (u64) resource(proctable(pids[2]), LOCAL_SPACE);
  discovery[2] = (u64) resource(proctable(pids[2]), LOCAL_ALLOC);
  REQUIRE(!spacewrite(info[0].space, 0x40050, discovery, sizeof(discovery)));
  REQUIRE(!procpublish(pids[0]) && !procpublish(pids[1]));
  for (;;) {
    REQUIRE(!procrunone() && ++turns < 100);
    if (!reused) {
      REQUIRE(!lifestat(info[0].life, &life));
      if (life.state == LIFE_ENDED) {
        REQUIRE(life.reason == LIFE_EXIT && life.code == 42);
        collect(pids[0], info[0].life);
        REQUIRE(!lifestat(info[2].life, &life) && life.state == LIFE_ABORTED);
        REQUIRE(!procnew(set, 1, 256, 512, LOCALS, &pids[3]) && !procinfo(pids[3], &info[3]));
        REQUIRE(handleindex(pids[0]) == handleindex(pids[3]) && pids[0] != pids[3]);
        cred.ruid = 210;
        cred.euid = 220;
        cred.rgid = 230;
        cred.egid = 240;
        REQUIRE(!lifecredentials(info[3].life, &cred));
        load(pids[3], &info[1], pids[2], 2);
        REQUIRE(!procpublish(pids[3]));
        reused = 1;
      }
    }
    if (reused && !proofgone) {
      REQUIRE(!spaceread(info[1].space, 0x40040, &marker, sizeof(marker)));
      if (marker) {
        REQUIRE(marker == 1);
        clean(pids[2]);
        REQUIRE(lifestat(info[2].life, &life) == -ERR_STALE);
        proofgone = 1;
      }
    }
    ended = 0;
    for (i = 1; i <= 3; i += 2) {
      if (i == 3 && !reused)
        continue;
      REQUIRE(!lifestat(info[i].life, &life));
      if (life.state == LIFE_ENDED) {
        if (life.reason != LIFE_EXIT || life.code != 42) {
          putstr("CREDRESULT actor=");
          puthex(i);
          putstr(" code=");
          puthex(life.code);
          putstr("\n");
          panic("credential user result");
        }
        ended++;
      }
    }
    if (ended == 2)
      break;
  }
  REQUIRE(reused && proofgone && turns >= 6);
  collect(pids[1], info[1].life);
  collect(pids[3], info[3].life);
  REQUIRE(!setstat(set, &group) && !group.charged && !group.instances && !setclose(set));
  pgstat(&after);
  REQUIRE(before.allocated == after.allocated && before.free == after.free && !pgcheck() &&
          !acctcheck());
  putstr("CREDCHECK turns=");
  puthex(turns);
  putstr(" reused=");
  puthex(reused);
  putstr(" proofgone=");
  puthex(proofgone);
  putstr(" charged=");
  puthex(group.charged);
  putstr(" allocated=");
  puthex(after.allocated);
  putstr("\n");
}
