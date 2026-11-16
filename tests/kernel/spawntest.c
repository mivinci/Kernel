#include "ktest.h"
#include <proc.h>
#include <signal.h>
#include <prep.h>
#include <abi/boot.h>
#include <diag.h>
extern const u8 spawn_begin[], spawn_end[], spawn_victim[], spawn_killer[];
extern const u8 spawn_child_begin[], spawn_child_end[];
#define REQUIRE(c)                                                                                 \
  do {                                                                                             \
    if (!(c)) {                                                                                    \
      putstr("SPAWNFAIL line=");                                                                   \
      puthex(__LINE__);                                                                            \
      putstr("\n");                                                                                \
      panic("native creation");                                                                    \
    }                                                                                              \
  } while (0)
static Handle
resource(Rtable *t, u32 kind)
{
  u32 i;
  for (i = 0; i < t->count; i++)
    if (t->slots[i].subtype == kind)
      return handlepack(HANDLE_RESOURCE, t->slots[i].generation, i);
  panic("spawn test boot resource");
}
static void
load(Handle pid, Setid ordinary, Setid service, u32 role, const Lifeid *victim)
{
  Rtable      *t = proctable(pid);
  Handle       allocator = resource(t, LOCAL_ALLOC), space = resource(t, LOCAL_SPACE);
  Handle       code, stack, boot, data;
  Allocarg     args;
  Boot         header;
  Config       c;
  Createpolicy policy;
  Cred         cred;
  Procinfo     info;
  u64          config[8], resources[8];
  u32          i;
  REQUIRE(!procinfo(pid, &info));
  cred.ruid = 10;
  cred.euid = 20;
  cred.rgid = 30;
  cred.egid = 40;
  REQUIRE(!lifecredentials(info.life, &cred));
  args.bytes = 8 * PAGE_SIZE;
  args.kind = ALLOC_CODE;
  args.align = args.minaddr = args.maxaddr = args.reserved = 0;
  REQUIRE(!localalloc(t, allocator, &args, &code));
  REQUIRE((u64) (spawn_end - spawn_begin) <= args.bytes &&
          (u64) (spawn_child_end - spawn_child_begin) <= PAGE_SIZE);
  REQUIRE(!localwrite(t, code, 0, spawn_begin, (u64) (spawn_end - spawn_begin)) &&
          !localseal(t, code));
  args.bytes = 65536;
  args.kind = ALLOC_DATA;
  REQUIRE(!localalloc(t, allocator, &args, &stack));
  args.bytes = PAGE_SIZE;
  REQUIRE(!localalloc(t, allocator, &args, &boot) && !localalloc(t, allocator, &args, &data));
  for (i = 0; i < sizeof(header); i++)
    ((u8 *) &header)[i] = 0;
  for (i = 0; i < 8; i++)
    config[i] = resources[i] = 0;
  header.magic = BOOT_MAGIC;
  header.format = 1;
  header.bytes = sizeof(header) + sizeof(resources);
  header.resource_count = 2;
  header.resources_off = sizeof(header);
  header.user_lo = 0x10000;
  header.user_hi = 1UL << 38;
  resources[0] = 1;
  resources[1] = (u64) space;
  resources[4] = 2;
  resources[5] = (u64) allocator;
  REQUIRE(!localwrite(t, boot, 0, &header, sizeof(header)) &&
          !localwrite(t, boot, sizeof(header), resources, sizeof(resources)));
  REQUIRE(!localmap(t, space, code, 0, 0x10000, 8 * PAGE_SIZE, VM_READ | VM_EXEC));
  REQUIRE(!localmap(t, space, stack, 0, 0x3ffffe0000UL, 65536, VM_READ | VM_WRITE));
  REQUIRE(!localmap(t, space, boot, 0, 0x3fff000000UL, PAGE_SIZE, VM_READ));
  REQUIRE(!localmap(t, space, data, 0, 0x40000, PAGE_SIZE, VM_READ | VM_WRITE));
  if (role != 2) {
    policy.memlimit = 128;
    policy.exec_limit = 256;
    policy.resources = 16;
    policy.pending = 2;
    for (i = 0; i < 4; i++) {
      policy.set = i >= 2 ? service : ordinary;
      policy.rights = i == 1   ? CREATE_USE | CREATE_CREDENTIAL
                      : i == 2 ? CREATE_RIGHTS | LOCAL_GRANT
                               : CREATE_USE;
      REQUIRE(!localcreate(t, &policy, (Handle *) &config[i]));
    }
  }
  if (victim)
    REQUIRE(!localcontrol(t, *victim, CONTROL_TERMINATE, (Handle *) &config[7]));
  config[4] = (u64) space;
  config[5] = (u64) allocator;
  config[6] = (u64) pid;
  REQUIRE(!localwrite(t, data, 0, config, sizeof(config)));
  c.entry = 0x10000 + (role == 2 ? (u64) (spawn_killer - spawn_begin)
                       : role    ? (u64) (spawn_victim - spawn_begin)
                                 : 0);
  c.stack = 0x3fffff0000UL;
  c.boot = 0x3fff000000UL;
  c.bootsz = header.bytes;
  c.sigmask = c.sigdefault = 0;
  REQUIRE(!procconfigure(pid, &c, 0));
}
static void
pump(void)
{
  u32 i;
  for (i = 0; i < 16; i++) {
    REQUIRE(procwork(32) >= 0);
    REQUIRE(prepwork(32) >= 0);
  }
}
static void
clean(Handle pid)
{
  int error;
  while ((error = procclean(pid)) > 0) {
  }
  REQUIRE(!error || error == -ERR_STALE);
}
static void
checkexit(Lifeid id, u32 reason, u32 code)
{
  Life life;
  REQUIRE(!lifestat(id, &life));
  if (life.state != LIFE_ENDED || life.reason != reason || life.code != code) {
    putstr("SPAWNRESULT pid=");
    puthex((u64) life.pid);
    putstr(" reason=");
    puthex(life.reason);
    putstr(" code=");
    puthex(life.code);
    putstr("\n");
    panic("spawn user result");
  }
}
void
spawntest(void)
{
  Setid      a, b;
  Set        basea, baseb, now;
  Pgstat     before, after;
  Handle     parent, victim, killer, children[4], value;
  Procinfo   source, target[4], partial[3], victiminfo, killerinfo, abandoned;
  Life       life;
  Liferesult result;
  u64        marker;
  u32        i, turns = 0, have[4], collected[3], phave[3], baseline = 0, dead;
  pgstat(&before);
  REQUIRE(!setcreate(1024, 2048, 16, &a) && !setcreate(512, 1024, 16, &b));
  REQUIRE(!procnew(a, 1, 256, 512, 64, &parent) && !procinfo(parent, &source));
  load(parent, a, b, 0, NULL);
  REQUIRE(!setstat(a, &basea) && !setstat(b, &baseb));
  for (i = 0; i < 4; i++)
    have[i] = 0;
  for (i = 0; i < 3; i++)
    collected[i] = phave[i] = 0;
  REQUIRE(!procpublish(parent));
  for (;;) {
    REQUIRE(!procrunone() && ++turns < 300);
    REQUIRE(!spaceread(source.space, 0x40080, &marker, sizeof(marker)));
    for (i = 0; i < 3; i++) {
      REQUIRE(!spaceread(source.space, 0x40208 + i * 16, &value, sizeof(value)));
      if (value && !phave[i] && !procinfo(value, &partial[i])) {
        phave[i] = 1;
      }
    }
    for (i = 0; i < 4; i++) {
      REQUIRE(!spaceread(source.space, 0x40090 + i * 8, &value, sizeof(value)));
      if (value && !have[i] && !procinfo(value, &target[i])) {
        children[i] = value;
        have[i] = 1;
      }
    }
    if (marker == 1 && !baseline) {
      REQUIRE(!setstat(a, &now) && now.charged == basea.charged &&
              now.instances == basea.instances && now.refs == basea.refs);
      REQUIRE(!setstat(b, &now) && now.charged == baseb.charged &&
              now.instances == baseb.instances);
      for (i = 0; i < 3; i++)
        REQUIRE(phave[i] && lifestat(partial[i].life, &life) == -ERR_STALE);
      baseline = 1;
    }
    for (i = 0; i < 3; i++) {
      if (!have[i] || collected[i])
        continue;
      REQUIRE(!lifestat(target[i].life, &life));
      if (life.state == LIFE_ENDED) {
        checkexit(target[i].life, LIFE_EXIT, 42);
        clean(children[i]);
        REQUIRE(!lifecollect(target[i].life, parent, &result) && result.code == 42);
        collected[i] = 1;
      }
    }
    REQUIRE(!lifestat(source.life, &life));
    if (life.state == LIFE_ENDED) {
      checkexit(source.life, LIFE_EXIT, 42);
      REQUIRE(marker == 2 && baseline);
      break;
    }
    pump();
  }
  clean(parent);
  REQUIRE(!lifecollect(source.life, 1, &result) && result.code == 42);
  pump();
  for (i = 0; i < 3; i++)
    REQUIRE(have[i] && collected[i] && lifestat(target[i].life, &life) == -ERR_STALE);
  REQUIRE(have[3] && lifestat(target[3].life, &life) == -ERR_STALE);
  REQUIRE(!setstat(a, &now) && !now.charged && !now.instances && !now.leases);
  REQUIRE(!setstat(b, &now) && !now.charged && !now.instances && !now.leases);
  /* A real second user actor kills the caller while native destroy is yielded. */
  REQUIRE(!procnew(a, 1, 256, 512, 64, &victim) && !procinfo(victim, &victiminfo));
  REQUIRE(!procnew(a, 1, 256, 512, 64, &killer) && !procinfo(killer, &killerinfo));
  load(victim, a, b, 1, NULL);
  load(killer, a, b, 2, &victiminfo.life);
  REQUIRE(!procpublish(victim) && !procrunone());
  turns++;
  REQUIRE(!spaceread(victiminfo.space, 0x40080, &marker, sizeof(marker)) && marker == 99);
  REQUIRE(!spaceread(victiminfo.space, 0x40088, &value, sizeof(value)) && value &&
          !procinfo(value, &abandoned));
  REQUIRE(!procpublish(killer));
  dead = 0;
  while (!dead) {
    REQUIRE(!procrunone() && ++turns < 320);
    REQUIRE(!lifestat(killerinfo.life, &life));
    dead = life.state == LIFE_ENDED;
  }
  checkexit(victiminfo.life, LIFE_SIGNAL, 9);
  checkexit(killerinfo.life, LIFE_EXIT, 42);
  clean(victim);
  clean(killer);
  pump();
  REQUIRE(!lifecollect(victiminfo.life, 1, &result) && result.reason == LIFE_SIGNAL);
  REQUIRE(!lifecollect(killerinfo.life, 1, &result) && result.code == 42);
  REQUIRE(lifestat(abandoned.life, &life) == -ERR_STALE);
  REQUIRE(!setstat(a, &now) && !now.instances && !now.charged && !now.leases && !setclose(a));
  REQUIRE(!setstat(b, &now) && !now.instances && !now.charged && !now.leases && !setclose(b));
  pgstat(&after);
  REQUIRE(before.allocated == after.allocated && before.free == after.free && !pgcheck() &&
          !acctcheck() && !sigcheck() && !groupcheck());
  putstr("SPAWNCHECK turns=");
  puthex(turns);
  putstr(" children=");
  puthex(3);
  putstr(" cancelled=");
  puthex(5);
  putstr(" abandoned=");
  puthex(1);
  putstr(" charged=");
  puthex(now.charged);
  putstr(" allocated=");
  puthex(after.allocated);
  putstr("\n");
}
