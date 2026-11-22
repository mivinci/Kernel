#include "ktest.h"
#include <proc.h>
#include <group.h>
#include <abi/boot.h>
#include <diag.h>
extern const u8 stats_begin[], stats_end[], stats_target[];
#define REQUIRE(c)                                                                                 \
  do {                                                                                             \
    if (!(c)) {                                                                                    \
      putstr("STATSFAIL line=");                                                                   \
      puthex(__LINE__);                                                                            \
      putstr("\n");                                                                                \
      panic("native statistics");                                                                  \
    }                                                                                              \
  } while (0)
static Handle
resource(Rtable *t, u32 kind)
{
  u32 i;
  for (i = 0; i < t->count; i++)
    if (t->slots[i].subtype == kind)
      return handlepack(HANDLE_RESOURCE, t->slots[i].generation, i);
  panic("statistics boot resource");
}
static void
load(Handle pid, Handle target, u32 role)
{
  Rtable  *t = proctable(pid);
  Handle   allocator = resource(t, LOCAL_ALLOC), space = resource(t, LOCAL_SPACE);
  Handle   code, stack, boot, data;
  Allocarg args;
  Config   c;
  Boot     header;
  u64      resources[8], config[8], entry = 0x10000;
  Procinfo info;
  Acct     account;
  u32      i;
  args.bytes = 4 * PAGE_SIZE;
  args.kind = ALLOC_CODE;
  args.align = args.minaddr = args.maxaddr = args.reserved = 0;
  REQUIRE(!localalloc(t, allocator, &args, &code));
  REQUIRE((u64) (stats_end - stats_begin) <= args.bytes);
  REQUIRE(!localwrite(t, code, 0, stats_begin, (u64) (stats_end - stats_begin)) &&
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
  header.format = BOOT_FORMAT;
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
  REQUIRE(!procinfo(target, &info) && !acctstat(info.acct, &account));
  config[2] = (u64) target;
  config[3] = (u64) pid;
  config[4] = account.used;
  if (!role) {
    REQUIRE(!localcontrol(t, info.life, CONTROL_QUERY, (Handle *) &config[0]));
    REQUIRE(!localendpoint(t, info.identity, LOCAL_NOTIFY, LOCAL_USE, 1UL << 32, 0,
                           (Handle *) &config[1]));
  }
  REQUIRE(!localwrite(t, data, 0, config, sizeof(config)));
  c.entry = entry + (role ? (u64) (stats_target - stats_begin) : 0);
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
    putstr("STATSRESULT code=");
    puthex(life.code);
    putstr("\n");
    panic("statistics user result");
  }
  clean(pid);
  REQUIRE(!lifecollect(identity, 1, &result));
}
void
statstest(void)
{
  Setid    set;
  Set      group;
  Handle   pids[3];
  Procinfo info[3];
  Rtable  *observer;
  Acct     account;
  Pgstat   before, after;
  u64      marker, replacement[3], user, kernel, blocked_user, blocked_kernel;
  u32      turns = 0;
  pgstat(&before);
  REQUIRE(!setcreate(4096, 8192, 8, &set));
  REQUIRE(!procnew(set, 1, 256, 512, LOCALS, &pids[0]) && !procinfo(pids[0], &info[0]));
  REQUIRE(!procnew(set, 1, 256, 512, LOCALS, &pids[1]) && !procinfo(pids[1], &info[1]));
  observer = proctable(pids[0]);
  load(pids[1], pids[1], 1);
  load(pids[0], pids[1], 0);
  REQUIRE(!procpublish(pids[0]) && !procrunone());
  turns++;
  REQUIRE(!spaceread(info[0].space, 0x40080, &marker, sizeof(marker)) && marker == 1);
  REQUIRE(!procusage(pids[0], &blocked_user, &blocked_kernel) && blocked_user && blocked_kernel);
  REQUIRE(!procpublish(pids[1]) && !procusage(pids[0], &user, &kernel) && user == blocked_user &&
          kernel == blocked_kernel);
  while (marker != 2) {
    REQUIRE(!procrunone() && ++turns < 10);
    REQUIRE(!spaceread(info[0].space, 0x40080, &marker, sizeof(marker)));
  }
  REQUIRE(!procusage(pids[1], &user, &kernel) && user && kernel);
  collect(pids[1], info[1].life);
  REQUIRE(!procnew(set, 1, 256, 512, LOCALS, &pids[2]) && !procinfo(pids[2], &info[2]));
  REQUIRE(handleindex(pids[2]) == handleindex(pids[1]) && pids[2] != pids[1]);
  REQUIRE(!procusage(pids[2], &user, &kernel) && !user && !kernel);
  REQUIRE(!localcontrol(observer, info[2].life, CONTROL_QUERY, (Handle *) &replacement[0]));
  replacement[1] = (u64) pids[2];
  REQUIRE(!acctstat(info[2].acct, &account));
  replacement[2] = account.used;
  REQUIRE(!spacewrite(info[0].space, 0x40028, replacement, sizeof(replacement)));
  REQUIRE(!procrunone());
  turns++;
  collect(pids[0], info[0].life);
  REQUIRE(!procdestroy(pids[2]));
  clean(pids[2]);
  REQUIRE(!setstat(set, &group) && !group.charged && !group.instances && !setclose(set));
  pgstat(&after);
  REQUIRE(before.allocated == after.allocated && before.free == after.free && !pgcheck() &&
          !acctcheck() && !groupcheck());
  putstr("STATSCHECK turns=");
  puthex(turns);
  putstr(" reused=");
  puthex(1);
  putstr(" charged=");
  puthex(group.charged);
  putstr(" allocated=");
  puthex(after.allocated);
  putstr("\n");
}
