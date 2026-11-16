#include "ktest.h"
#include <proc.h>
#include <abi/boot.h>
#include <diag.h>
extern const u8 event_begin[], event_end[], event_server[];
#define REQUIRE(c)                                                                                 \
  do {                                                                                             \
    if (!(c)) {                                                                                    \
      putstr("EVENTFAIL line=");                                                                   \
      puthex(__LINE__);                                                                            \
      putstr("\n");                                                                                \
      panic("native events");                                                                      \
    }                                                                                              \
  } while (0)
static Handle
resource(Rtable *t, u32 kind)
{
  u32 i;
  for (i = 0; i < t->count; i++)
    if (t->slots[i].subtype == kind)
      return handlepack(HANDLE_RESOURCE, t->slots[i].generation, i);
  panic("native boot resource");
}
static void
load(Handle pid, u64 entry, u32 server, Handle other, Lifeid selflife, Handle otherpid, Handle send)
{
  Rtable *t = proctable(pid);
  Handle  allocator = resource(t, LOCAL_ALLOC), space = resource(t, LOCAL_SPACE), code, stack, boot,
         data;
  Allocarg args;
  Config   c;
  Boot     header;
  u64      resources[8], config[7];
  u32      i;
  args.bytes = 4 * PAGE_SIZE;
  args.kind = ALLOC_CODE;
  args.align = args.minaddr = args.maxaddr = args.reserved = 0;
  REQUIRE(!localalloc(t, allocator, &args, &code));
  REQUIRE((u64) (event_end - event_begin) <= 4 * PAGE_SIZE);
  REQUIRE(!localwrite(t, code, 0, event_begin, (u64) (event_end - event_begin)) &&
          !localseal(t, code));
  args.bytes = 65536;
  args.kind = ALLOC_DATA;
  REQUIRE(!localalloc(t, allocator, &args, &stack));
  args.bytes = PAGE_SIZE;
  REQUIRE(!localalloc(t, allocator, &args, &boot));
  for (i = 0; i < sizeof(header); i++)
    ((u8 *) &header)[i] = 0;
  header.magic = BOOT_MAGIC;
  header.format = 1;
  header.bytes = sizeof(header) + sizeof(resources);
  header.resource_count = 2;
  header.resources_off = sizeof(header);
  header.user_lo = 0x10000;
  header.user_hi = 1UL << 38;
  for (i = 0; i < 8; i++)
    resources[i] = 0;
  resources[0] = 1;
  resources[1] = (u64) space;
  resources[4] = 2;
  resources[5] = (u64) allocator;
  REQUIRE(!localwrite(t, boot, 0, &header, sizeof(header)) &&
          !localwrite(t, boot, sizeof(header), resources, sizeof(resources)));
  REQUIRE(!localmap(t, space, code, 0, entry, 4 * PAGE_SIZE, VM_READ | VM_EXEC));
  REQUIRE(!localmap(t, space, stack, 0, 0x3ffffe0000UL, 65536, VM_READ | VM_WRITE));
  REQUIRE(!localmap(t, space, boot, 0, 0x3fff000000UL, PAGE_SIZE, VM_READ));
  REQUIRE(!localalloc(t, allocator, &args, &data));
  REQUIRE(!localmap(t, space, data, 0, 0x40000, PAGE_SIZE, VM_READ | VM_WRITE));
  REQUIRE(!localcontrol(t, selflife, CONTROL_CONNECT, (Handle *) &config[1]));
  REQUIRE(!localcontrol(t, selflife, CONTROL_CONNECT, (Handle *) &config[5]));
  REQUIRE(!localcontrol(t, selflife, CONTROL_QUERY | CONTROL_INSTALL | LOCAL_GRANT,
                        (Handle *) &config[6]));
  config[0] = (u64) other;
  config[3] = (u64) otherpid;
  config[4] = (u64) pid;
  config[2] = (u64) send;
  REQUIRE(!localwrite(t, data, 0, config, sizeof(config)));
  c.entry = entry + (server ? (u64) (event_server - event_begin) : 0);
  c.stack = 0x3fffff0000UL;
  c.boot = 0x3fff000000UL;
  c.bootsz = header.bytes;
  c.sigmask = c.sigdefault = 0;
  REQUIRE(!procconfigure(pid, &c, 0));
}

void
eventtest(void)
{
  Setid      set;
  Set        group;
  Handle     pids[2], calls[2], notify[2];
  Procinfo   info[2];
  Life       life;
  Liferesult result;
  Pgstat     before, after;
  u32        i, turns = 0, idle = 0, ended;
  int        error;
  pgstat(&before);
  REQUIRE(!setcreate(4096, 8192, 8, &set));
  for (i = 0; i < 2; i++)
    REQUIRE(!procnew(set, 1, 256, 512, LOCALS, &pids[i]) && !procinfo(pids[i], &info[i]));
  for (i = 0; i < 2; i++) {
    REQUIRE(!localcontrol(proctable(pids[i]), info[1 - i].life, CONTROL_CONNECT, &calls[i]));
    REQUIRE(!localcontrol(proctable(pids[i]), info[1 - i].life, CONTROL_CONNECT, &notify[i]));
    load(pids[i], 0x10000 + (u64) i * 0x10000, i, calls[i], info[i].life, pids[1 - i], notify[i]);
  }
  REQUIRE(!procpublish(pids[0]) && !procpublish(pids[1]) && !proctimerstart());
  for (;;) {
    error = procrunone();
    if (!error)
      REQUIRE(++turns < 1000);
    else
      REQUIRE(error == -ERR_WOULD_BLOCK);
    ended = 0;
    for (i = 0; i < 2; i++) {
      REQUIRE(!lifestat(info[i].life, &life));
      if (life.state == LIFE_ENDED) {
        if (life.reason != LIFE_EXIT || life.code != 42) {
          putstr("EVENTRESULT actor=");
          puthex(i);
          putstr(" code=");
          puthex(life.code);
          putstr("\n");
          panic("event user result");
        }
        ended++;
      }
    }
    if (ended == 2)
      break;
    if (error)
      REQUIRE(procidle() == 1 && ++idle < 1000);
  }
  REQUIRE(idle && turns >= 4);
  for (i = 0; i < 2; i++) {
    while ((error = procclean(pids[i])) > 0) {
    }
    REQUIRE(!error && !lifecollect(info[i].life, 1, &result) && result.code == 42);
  }
  REQUIRE(!setstat(set, &group) && !group.charged && !group.instances && !setclose(set));
  pgstat(&after);
  REQUIRE(before.allocated == after.allocated && before.free == after.free && !pgcheck() &&
          !acctcheck());
  putstr("EVENTCHECK turns=");
  puthex(turns);
  putstr(" idle=");
  puthex(idle);
  putstr(" exits=");
  puthex(2);
  putstr(" charged=");
  puthex(group.charged);
  putstr(" allocated=");
  puthex(after.allocated);
  putstr("\n");
}
