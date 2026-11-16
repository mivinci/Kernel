#include "ktest.h"
#include <proc.h>
#include <bootimage.h>
#include <diag.h>
extern const u8       libsys_image[], libsys_image_end[];
typedef struct Packet Packet;
struct Packet
{
  Boot     header;
  Strref   argv, env;
  Resentry resources[4];
  char     arg[8], environment[16];
};
#define REQUIRE(c)                                                                                 \
  do {                                                                                             \
    if (!(c)) {                                                                                    \
      putstr("LIBSYSFAIL line=");                                                                  \
      puthex(__LINE__);                                                                            \
      putstr("\n");                                                                                \
      panic("libsys user ELF");                                                                    \
    }                                                                                              \
  } while (0)
static Handle
resource(Rtable *table, u32 kind)
{
  u32 i;
  for (i = 0; i < table->count; i++)
    if (table->slots[i].subtype == kind)
      return handlepack(HANDLE_RESOURCE, table->slots[i].generation, i);
  panic("libsys fixture resource");
}
static void
load(Handle pid, const Procinfo *peer, u32 client)
{
  Rtable          *table = proctable(pid);
  const Bootimage *image;
  const Bootseg   *segments;
  Handle           space = resource(table, LOCAL_SPACE), allocator = resource(table, LOCAL_ALLOC);
  Handle           memory, stack, boot, channel, sender;
  Allocarg         args;
  Config           config;
  Packet           packet;
  u64              i, page, copied;
  const char      *arg = client ? "client" : "server";
  const char      *env = "SAMPLE=yes";
  REQUIRE(!bootimagecheck(libsys_image, (u64) (libsys_image_end - libsys_image), &image));
  segments = (const Bootseg *) (libsys_image + image->segments_off);
  args.bytes = PAGE_SIZE;
  args.align = args.minaddr = args.maxaddr = args.reserved = 0;
  for (i = 0; i < image->segment_count; i++) {
    const Bootseg *segment = &segments[i];
    for (page = 0; page < segment->mem_bytes; page += PAGE_SIZE) {
      args.kind = segment->prot & VM_EXEC ? ALLOC_CODE : ALLOC_DATA;
      REQUIRE(!localalloc(table, allocator, &args, &memory));
      if (page < segment->file_bytes) {
        copied = MIN(PAGE_SIZE, segment->file_bytes - page);
        REQUIRE(!localwrite(table, memory, 0, libsys_image + segment->file_off + page, copied));
      }
      if (segment->prot & VM_EXEC)
        REQUIRE(!localseal(table, memory));
      REQUIRE(!localmap(table, space, memory, 0, segment->va + page, PAGE_SIZE, segment->prot));
    }
  }
  args.bytes = 65536;
  args.kind = ALLOC_DATA;
  REQUIRE(!localalloc(table, allocator, &args, &stack));
  REQUIRE(!localmap(table, space, stack, 0, 0x3ffffe0000UL, 65536, VM_READ | VM_WRITE));
  args.bytes = PAGE_SIZE;
  REQUIRE(!localalloc(table, allocator, &args, &boot));
  REQUIRE(!localendpoint(table, peer->identity, LOCAL_CALL, LOCAL_USE, 0, 0, &channel));
  REQUIRE(!localendpoint(table, peer->identity, LOCAL_SEND, LOCAL_USE, 0, 0, &sender));
  for (i = 0; i < sizeof(packet); i++)
    ((u8 *) &packet)[i] = 0;
  for (i = 0; i < sizeof(Boot); i++)
    ((u8 *) &packet.header)[i] = libsys_image[image->boot_off + i];
  packet.header.bytes = sizeof(packet);
  packet.header.argc = packet.header.envc = 1;
  packet.header.argv_off = offsetof(Packet, argv);
  packet.header.env_off = offsetof(Packet, env);
  packet.header.resource_count = 4;
  packet.header.resources_off = offsetof(Packet, resources);
  packet.argv.offset = offsetof(Packet, arg);
  packet.argv.length = 7;
  packet.env.offset = offsetof(Packet, environment);
  packet.env.length = 11;
  for (i = 0; i < 7; i++)
    packet.arg[i] = arg[i];
  for (i = 0; i < 11; i++)
    packet.environment[i] = env[i];
  packet.resources[0].tag = BOOT_SELF_SPACE;
  packet.resources[0].handle = space;
  packet.resources[1].tag = BOOT_SELF_ALLOC;
  packet.resources[1].handle = allocator;
  packet.resources[2].tag = 256;
  packet.resources[2].handle = channel;
  packet.resources[3].tag = 257;
  packet.resources[3].handle = sender;
  REQUIRE(!localwrite(table, boot, 0, &packet, sizeof(packet)));
  REQUIRE(!localmap(table, space, boot, 0, 0x3fff000000UL, PAGE_SIZE, VM_READ));
  config.entry = image->entry;
  config.stack = 0x3fffff0000UL;
  config.boot = 0x3fff000000UL;
  config.bootsz = sizeof(packet);
  config.sigmask = config.sigdefault = 0;
  REQUIRE(!procconfigure(pid, &config, 0));
}
void
libsysload(void)
{
  Setid      set;
  Set        group;
  Handle     pids[2];
  Procinfo   info[2];
  Life       life;
  Liferesult result;
  Pgstat     before, after;
  u32        i, ended, turns = 0;
  int        error;
  pgstat(&before);
  REQUIRE(!setcreate(4096, 8192, 8, &set));
  for (i = 0; i < 2; i++)
    REQUIRE(!procnew(set, 1, 256, 512, LOCALS, &pids[i]) && !procinfo(pids[i], &info[i]));
  load(pids[0], &info[1], 1);
  load(pids[1], &info[0], 0);
  REQUIRE(!procpublish(pids[0]) && !procpublish(pids[1]));
  do {
    REQUIRE(!procrunone() && ++turns < 64);
    ended = 0;
    for (i = 0; i < 2; i++) {
      REQUIRE(!lifestat(info[i].life, &life));
      if (life.state == LIFE_ENDED) {
        if (life.reason != LIFE_EXIT || life.code != 42) {
          putstr("LIBSYSRESULT actor=");
          puthex(i);
          putstr(" reason=");
          puthex(life.reason);
          putstr(" code=");
          puthex(life.code);
          putstr("\n");
          panic("libsys custom entry result");
        }
        ended++;
      }
    }
  } while (ended != 2);
  for (i = 0; i < 2; i++) {
    while ((error = procclean(pids[i])) > 0) {
    }
    REQUIRE(!error && !lifecollect(info[i].life, 1, &result) && result.code == 42);
  }
  REQUIRE(!setstat(set, &group) && !group.charged && !group.instances && !setclose(set));
  pgstat(&after);
  REQUIRE(before.allocated == after.allocated && before.free == after.free && !pgcheck() &&
          !acctcheck());
  putstr("LIBSYSCHECK actors=");
  puthex(2);
  putstr(" turns=");
  puthex(turns);
  putstr(" charged=");
  puthex(group.charged);
  putstr(" allocated=");
  puthex(after.allocated);
  putstr("\n");
}
