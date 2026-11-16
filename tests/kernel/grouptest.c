#include "ktest.h"
#include <proc.h>
#include <group.h>
#include <abi/boot.h>
#include <diag.h>
extern const u8 group_begin[], group_end[], group_descendant[];
#define REQUIRE(c)                                                                                 \
  do {                                                                                             \
    if (!(c)) {                                                                                    \
      putstr("GROUPFAIL line=");                                                                   \
      puthex(__LINE__);                                                                            \
      putstr("\n");                                                                                \
      panic("native groups");                                                                      \
    }                                                                                              \
  } while (0)
static Handle
resource(Rtable *t, u32 kind)
{
  u32 i;
  for (i = 0; i < t->count; i++)
    if (t->slots[i].subtype == kind)
      return handlepack(HANDLE_RESOURCE, t->slots[i].generation, i);
  panic("group boot resource");
}
static void
load(Handle pid, Handle child, Handle stranger, Handle parent, Handle replacement, u32 role)
{
  Rtable  *t = proctable(pid);
  Handle   allocator = resource(t, LOCAL_ALLOC), space = resource(t, LOCAL_SPACE);
  Handle   code, stack, boot, data;
  Allocarg args;
  Config   c;
  Boot     header;
  u64      resources[8], config[8], entry = 0x10000;
  u32      i;
  args.bytes = 4 * PAGE_SIZE;
  args.kind = ALLOC_CODE;
  args.align = args.minaddr = args.maxaddr = args.reserved = 0;
  REQUIRE(!localalloc(t, allocator, &args, &code));
  REQUIRE((u64) (group_end - group_begin) <= args.bytes);
  REQUIRE(!localwrite(t, code, 0, group_begin, (u64) (group_end - group_begin)) &&
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
  for (i = 0; i < 8; i++)
    config[i] = 0;
  config[0] = (u64) child;
  config[1] = (u64) stranger;
  config[2] = (u64) pid;
  config[4] = (u64) parent;
  config[5] = (u64) replacement;
  REQUIRE(!localwrite(t, data, 0, config, sizeof(config)));
  c.entry = entry + (role ? (u64) (group_descendant - group_begin) : 0);
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
  Life       life;
  Liferesult result;
  REQUIRE(!lifestat(identity, &life));
  if (life.reason != LIFE_EXIT || life.code != 42) {
    putstr("GROUPRESULT code=");
    puthex(life.code);
    putstr("\n");
    panic("group user result");
  }
  clean(pid);
  REQUIRE(!lifecollect(identity, 1, &result));
}
void
grouptest(void)
{
  Setid    set;
  Set      account;
  Handle   pids[5], pgid, sid, inherited, replacement, construct;
  Procinfo info[5];
  Life     life;
  Group    relation;
  Pgstat   before, after;
  u64      marker, descendant;
  u32      i, turns = 0;
  pgstat(&before);
  REQUIRE(!setcreate(4096, 8192, 8, &set));
  for (i = 0; i < 3; i++)
    REQUIRE(!procnew(set, i == 1 ? pids[0] : 1, 256, 512, LOCALS, &pids[i]) &&
            !procinfo(pids[i], &info[i]));
  load(pids[0], pids[1], pids[2], 0, 0, 0);
  REQUIRE(!procprepare(proctable(pids[0]), pids[1], 31, &construct));
  REQUIRE(!procpublish(pids[0]) && !procrunone());
  turns++;
  REQUIRE(!lifestat(info[0].life, &life) && life.state == LIFE_LIVE);
  REQUIRE(!spaceread(info[0].space, 0x40080, &marker, sizeof(marker)) && marker == 1);
  REQUIRE(!procnew(set, pids[0], 256, 512, LOCALS, &pids[3]) && !procinfo(pids[3], &info[3]));
  load(pids[3], 0, pids[2], pids[0], 0, 1);
  REQUIRE(!lifegroup(info[3].life, &inherited) && !groupnames(inherited, &pgid, &sid));
  REQUIRE(pgid == pids[0] && sid == pids[0]);
  descendant = (u64) pids[3];
  REQUIRE(!spacewrite(info[0].space, 0x40018, &descendant, sizeof(descendant)));
  REQUIRE(!procpublish(pids[3]) && !procrunone());
  turns++;
  collect(pids[0], info[0].life);
  REQUIRE(!groupstat(inherited, &relation) && relation.processes == 1 && relation.members == 1);
  REQUIRE(!groupnames(inherited, &pgid, &sid) && pgid == pids[0] && sid == pids[0]);
  REQUIRE(!lifestat(info[1].life, &life) && life.state == LIFE_ABORTED);
  clean(pids[1]);
  REQUIRE(!procdestroy(pids[2]));
  clean(pids[2]);
  REQUIRE(!procnew(set, 1, 256, 512, LOCALS, &pids[4]) && !procinfo(pids[4], &info[4]));
  REQUIRE(handleindex(pids[4]) == handleindex(pids[0]) && pids[4] != pids[0]);
  replacement = pids[4];
  REQUIRE(!spacewrite(info[3].space, 0x40028, &replacement, sizeof(replacement)));
  /* A manager keeps the old group joinable while its last member moves. */
  REQUIRE(!grouphold(inherited));
  REQUIRE(!procrunone());
  turns++;
  /* Full orphan adoption is a later lifecycle slice. */
  REQUIRE(!lifereparent(info[3].life, pids[0], 1));
  collect(pids[3], info[3].life);
  REQUIRE(!groupdrop(inherited));
  REQUIRE(groupstat(inherited, &relation) == -ERR_BAD_HANDLE);
  REQUIRE(!procdestroy(pids[4]));
  clean(pids[4]);
  REQUIRE(!groupidused(pids[0]) && !groupidused(pids[3]) && !groupcheck());
  REQUIRE(!setstat(set, &account) && !account.charged && !account.instances && !setclose(set));
  pgstat(&after);
  REQUIRE(before.allocated == after.allocated && before.free == after.free && !pgcheck() &&
          !acctcheck());
  putstr("GROUPCHECK turns=");
  puthex(turns);
  putstr(" reused=");
  puthex(1);
  putstr(" charged=");
  puthex(account.charged);
  putstr(" allocated=");
  puthex(after.allocated);
  putstr("\n");
}
