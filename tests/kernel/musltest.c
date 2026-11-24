#include "load.h"
#include <diag.h>
#include <abi/boot.h>
#include <rq.h>

extern const u8  musl_image[], musl_image_end[];
extern const u64 musl_role_address, musl_turn_address;

#define REQUIRE(c)                                                                                 \
  do {                                                                                             \
    if (!(c)) {                                                                                    \
      putstr("MUSLFAIL line=");                                                                    \
      puthex(__LINE__);                                                                            \
      putstr("\n");                                                                                \
      panic("musl TLS isolation");                                                                 \
    }                                                                                              \
  } while (0)

void
musltest(void)
{
  struct
  {
    Boot     header;
    Resentry entries[3];
  } packet;
  Setid    set;
  Handle   pid[2];
  Procinfo p[2];
  u64      turn, role, observed;
  u32      i;
  for (i = 0; i < sizeof(packet); i++)
    ((u8 *) &packet)[i] = 0;
  REQUIRE(!setcreate(1024, 2048, 4, &set));
  packet.header.magic = BOOT_MAGIC;
  packet.header.format = BOOT_FORMAT;
  packet.header.bytes = sizeof(packet);
  packet.header.resource_count = 2;
  packet.header.resources_off = sizeof(Boot);
  packet.header.user_lo = BOOT_USER_LO;
  packet.header.user_hi = BOOT_USER_HI;
  packet.entries[0].tag = BOOT_SELF_SPACE;
  packet.entries[1].tag = BOOT_SELF_ALLOC;
  for (i = 0; i < 2; i++) {
    Rtable      *table;
    Createpolicy policy = {0};
    REQUIRE(!procnew(set, 1, 256, 512, LOCALS, &pid[i]) && !procinfo(pid[i], &p[i]));
    table = proctable(pid[i]);
    REQUIRE(table);
    packet.entries[0].handle = testresource(table, LOCAL_SPACE);
    packet.entries[1].handle = testresource(table, LOCAL_ALLOC);
    packet.header.resource_count = i ? 2 : 3;
    if (!i) {
      policy.set = set;
      policy.memlimit = 256;
      policy.exec_limit = 512;
      policy.resources = LOCALS;
      policy.pending = PENDING_CALLS;
      policy.rights = CREATE_USE;
      packet.entries[2].tag = BOOT_CREATE;
      REQUIRE(!localcreate(table, &policy, &packet.entries[2].handle));
    } else {
      packet.entries[2].tag = 0;
      packet.entries[2].handle = 0;
    }
    testloadmapped(pid[i], musl_image, musl_image_end - musl_image, &packet, sizeof(packet));
    role = i + 1;
    REQUIRE(!spacewrite(p[i].space, musl_role_address, &role, sizeof(role)) &&
            !procpublish(pid[i]));
  }
  for (turn = 1; turn <= 4; turn++)
    for (i = 0; i < 2; i++)
      REQUIRE(!procrunone() &&
              !spaceread(p[i].space, musl_turn_address, &observed, sizeof(observed)) &&
              observed == turn);
  for (turn = 0; turn < 32; turn++) {
    Life first, second;
    REQUIRE(!lifestat(p[0].life, &first) && !lifestat(p[1].life, &second));
    if (first.state == LIFE_ENDED && second.state == LIFE_ENDED)
      break;
    REQUIRE(!procrunone());
  }
  for (i = 0; i < 2; i++) {
    Life       life;
    Liferesult result;
    int        error;
    REQUIRE(!lifestat(p[i].life, &life));
    if (life.reason != LIFE_EXIT || life.code != 42) {
      putstr("MUSLEXIT actor=");
      puthex(i);
      putstr(" reason=");
      puthex(life.reason);
      putstr(" code=");
      puthex(life.code);
      putstr("\n");
    }
    REQUIRE(life.state == LIFE_ENDED && life.reason == LIFE_EXIT && life.code == 42);
    while ((error = procclean(pid[i])) > 0) {
    }
    REQUIRE(!error && !lifecollect(p[i].life, 1, &result));
  }
  for (i = 0; i < 2048; i++) {
    Set budget;
    REQUIRE(!setstat(set, &budget));
    if (!budget.charged && !budget.instances && budget.refs == 1)
      break;
    REQUIRE(procwork(32) >= 0);
  }
  REQUIRE(!setclose(set));
}
