#include "ktest.h"
#include <proc.h>
#include <bootimage.h>
#include "load.h"
#include <diag.h>
extern const u8       libsys_image[], libsys_image_end[];
extern const u64      libsys_resource_address;
typedef struct Packet Packet;
struct Packet
{
  Boot     header;
  Strref   argv, env;
  Resentry resources[6];
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
  Rtable   *table = proctable(pid);
  Bootimage image;
  Handle    space = testresource(table, LOCAL_SPACE), allocator = testresource(table, LOCAL_ALLOC);
  Handle    channel, sender, management, backend;
  Packet    packet;
  Config    reserve;
  u64       i;
  const char *arg = client ? "client" : "server";
  const char *env = "SAMPLE=yes";
  REQUIRE(!bootimagecheck(libsys_image, (u64) (libsys_image_end - libsys_image), &image));
  REQUIRE(!localendpoint(table, peer->identity, LOCAL_CALL, LOCAL_USE, 0, 0, &channel));
  REQUIRE(!localendpoint(table, peer->identity, LOCAL_SEND, LOCAL_USE, 0, 0, &sender));
  REQUIRE(!localcontrol(table, peer->life, CONTROL_INSTALL | CONTROL_REVOKE | CONTROL_QUERY,
                        &management));
  {
    Procinfo self;
    REQUIRE(!procinfo(pid, &self));
    REQUIRE(
        !localendpoint(table, self.identity, LOCAL_CALL, LOCAL_USE | LOCAL_GRANT, 0, 0, &backend));
  }
  for (i = 0; i < sizeof(packet); i++)
    ((u8 *) &packet)[i] = 0;
  for (i = 0; i < sizeof(Boot); i++)
    ((u8 *) &packet.header)[i] = libsys_image[image.boot_off + i];
  packet.header.bytes = sizeof(packet);
  packet.header.argc = packet.header.envc = 1;
  packet.header.argv_off = offsetof(Packet, argv);
  packet.header.env_off = offsetof(Packet, env);
  packet.header.resource_count = 6;
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
  packet.resources[4].tag = 258;
  packet.resources[4].handle = management;
  packet.resources[5].tag = 259;
  packet.resources[5].handle = backend;
  testload(pid, libsys_image, (u64) (libsys_image_end - libsys_image), &packet, sizeof(packet));
  reserve.entry = image.entry;
  reserve.stack = 0x3fffff0000UL;
  reserve.boot = 0x3fff000000UL;
  reserve.bootsz = sizeof(packet);
  reserve.sigmask = reserve.sigdefault = 0;
  REQUIRE(!procconfigure(pid, &reserve, client ? 2 : 0));
}
void
libsysload(void)
{
  Setid      sets[2];
  Set        group;
  Acct       payer;
  Handle     pids[2];
  Procinfo   info[2];
  Life       life;
  Liferesult result;
  Pgstat     before, after;
  u32        i, ended, turns = 0, source_cleaned = 0, resource_commits = 0;
  u64        handoff[16];
  int        error;
  pgstat(&before);
  for (i = 0; i < 2; i++) {
    REQUIRE(!setcreate(4096, 8192, 8, &sets[i]));
    REQUIRE(!procnew(sets[i], 1, 256, 512, LOCALS, &pids[i]) && !procinfo(pids[i], &info[i]));
  }
  load(pids[0], &info[1], 1);
  load(pids[1], &info[0], 0);
  REQUIRE(!procpublish(pids[0]) && !procpublish(pids[1]));
  do {
    REQUIRE(!procrunone() && ++turns < 64);
    REQUIRE(!spaceread(info[1].space, libsys_resource_address, handoff, sizeof(handoff)));
    REQUIRE(handoff[10] >= resource_commits && handoff[10] <= 2);
    resource_commits = (u32) handoff[10];
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
        if (!i && !source_cleaned) {
          u64 client_trace[16];
          REQUIRE(!spaceread(info[0].space, libsys_resource_address, client_trace,
                             sizeof(client_trace)) &&
                  client_trace[9] == 5);
          while ((error = procclean(pids[0])) > 0) {
          }
          REQUIRE(!error && !lifecollect(info[0].life, 1, &result) && result.code == 42);
          REQUIRE(!acctstat(info[0].acct, &payer) && payer.state == AC_CLOSING && payer.used == 3 &&
                  !payer.bound);
          REQUIRE(!setstat(sets[0], &group) && !group.instances && group.charged == 3);
          source_cleaned = 1;
        }
      }
    }
  } while (ended != 2);
  for (i = 1; i < 2; i++) {
    while ((error = procclean(pids[i])) > 0) {
    }
    REQUIRE(!error && !lifecollect(info[i].life, 1, &result) && result.code == 42);
  }
  REQUIRE(resource_commits == 2);
  REQUIRE(handoff[11] == 25 && handoff[12] == 1 && handoff[13] == 1 && handoff[14] == 1 &&
          handoff[15] == 1 && handoff[9] == 4);
  REQUIRE(source_cleaned && acctstat(info[0].acct, &payer) == AC_BAD);
  for (i = 0; i < 2; i++)
    REQUIRE(!setstat(sets[i], &group) && !group.charged && !group.instances && !setclose(sets[i]));
  pgstat(&after);
  REQUIRE(before.allocated == after.allocated && before.free == after.free && !pgcheck() &&
          !acctcheck());
  putstr("LIBSYSCHECK actors=");
  puthex(2);
  putstr(" resource=");
  puthex(resource_commits);
  putstr(" native=");
  puthex(handoff[11]);
  putstr(" late=");
  puthex(handoff[12]);
  putstr(" consent=");
  puthex(handoff[13]);
  putstr(" accepted=");
  puthex(handoff[14]);
  putstr(" peer_lookup=");
  puthex(handoff[15]);
  putstr(" hello=");
  puthex(1);
  putstr(" reuse=");
  puthex(1);
  putstr(" slot_ops=");
  puthex(1);
  putstr(" file_refs=");
  puthex(1);
  putstr(" control_close=");
  puthex(handoff[9] - 3);
  putstr(" ordinary_full=");
  puthex(8);
  putstr(" receive=");
  puthex(1);
  putstr(" shared=");
  puthex(1);
  putstr(" payer_exit=");
  puthex(source_cleaned);
  putstr(" turns=");
  puthex(turns);
  putstr(" charged=");
  puthex(group.charged);
  putstr(" allocated=");
  puthex(after.allocated);
  putstr("\n");
}
