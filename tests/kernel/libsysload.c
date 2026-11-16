#include "ktest.h"
#include <proc.h>
#include <bootimage.h>
#include "load.h"
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
static void
load(Handle pid, const Procinfo *peer, u32 client)
{
  Rtable          *table = proctable(pid);
  const Bootimage *image;
  Handle space = testresource(table, LOCAL_SPACE), allocator = testresource(table, LOCAL_ALLOC);
  Handle channel, sender;
  Packet packet;
  u64    i;
  const char *arg = client ? "client" : "server";
  const char *env = "SAMPLE=yes";
  REQUIRE(!bootimagecheck(libsys_image, (u64) (libsys_image_end - libsys_image), &image));
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
  testload(pid, libsys_image, (u64) (libsys_image_end - libsys_image), &packet, sizeof(packet));
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
