#include "ktest.h"
#include "load.h"
#include <abi/boot.h>
#include <signal.h>
#include <prep.h>
#include <diag.h>
extern const u8  sig_image[], sig_image_end[];
extern const u64 sig_trace_address;
#define REQUIRE(c)                                                                                 \
  do {                                                                                             \
    if (!(c)) {                                                                                    \
      putstr("SIGCONFIGFAIL line=");                                                               \
      puthex(__LINE__);                                                                            \
      putstr("\n");                                                                                \
      panic("signal configuration");                                                               \
    }                                                                                              \
  } while (0)
void
sigconfigtest(void)
{
  Setid        set;
  Set          account;
  Handle       pid, create;
  Procinfo     parent, children[6];
  Rtable      *table;
  Createpolicy policy;
  struct
  {
    Boot     header;
    Resentry resources[3];
  } boot;
  Sigstat          signal;
  Sigact           action;
  Life             life;
  Liferesult       result;
  Pgstat           before, after;
  u64              trace[24];
  u32              i, have[6], turns = 0, automatic = 0, retained = 0;
  int              error;
  static const u32 expected_policy[6] = {0, 0, 3, 0, 0, 3};
  pgstat(&before);
  REQUIRE(!setcreate(8192, 16384, 16, &set));
  REQUIRE(!procnew(set, 1, 512, 1024, LOCALS, &pid) && !procinfo(pid, &parent));
  table = proctable(pid);
  policy.set = set;
  policy.memlimit = 512;
  policy.exec_limit = 1024;
  policy.resources = LOCALS;
  policy.pending = 16;
  policy.rights = CREATE_USE;
  REQUIRE(!localcreate(table, &policy, &create));
  for (i = 0; i < sizeof(boot); i++)
    ((u8 *) &boot)[i] = 0;
  boot.header.magic = BOOT_MAGIC;
  boot.header.format = BOOT_FORMAT;
  boot.header.bytes = sizeof(boot);
  boot.header.resource_count = 3;
  boot.header.resources_off = sizeof(Boot);
  boot.header.user_lo = BOOT_USER_LO;
  boot.header.user_hi = BOOT_USER_HI;
  boot.resources[0].tag = BOOT_SELF_SPACE;
  boot.resources[0].handle = testresource(table, LOCAL_SPACE);
  boot.resources[1].tag = BOOT_SELF_ALLOC;
  boot.resources[1].handle = testresource(table, LOCAL_ALLOC);
  boot.resources[2].tag = BOOT_CREATE;
  boot.resources[2].handle = create;
  testload(pid, sig_image, (u64) (sig_image_end - sig_image), &boot, sizeof(boot));
  for (i = 0; i < 6; i++)
    have[i] = 0;
  REQUIRE(!procpublish(pid));
  for (;;) {
    REQUIRE(!procrunone() && ++turns < 256);
    REQUIRE(!spaceread(parent.space, sig_trace_address, trace, sizeof(trace)));
    if (trace[1]) {
      putstr("SIGCONFIGRESULT phase=");
      puthex(trace[2]);
      putstr(" code=");
      puthex(trace[1]);
      putstr("\n");
      panic("signal config user result");
    }
    for (i = 0; i < 6; i++) {
      if (trace[8 + i] && !have[i] && !procinfo((Handle) trace[8 + i], &children[i])) {
        have[i] = 1;
        REQUIRE(!lifestat(children[i].life, &life) && life.child_policy == expected_policy[i]);
        REQUIRE(!sigstat(children[i].identity, &signal) &&
                signal.mask == ((u64) 1 << ((i == 4 ? SIGNAL_USR2 : SIGNAL_USR1) - 1)));
        REQUIRE(!siggetaction(children[i].identity, SIGNAL_CHLD, &action) &&
                action.kind == (expected_policy[i] ? SIGNAL_IGNORE : SIGNAL_DEFAULT) &&
                !action.flags);
      }
      if (have[i] && !lifestat(children[i].life, &life) && life.state == LIFE_ENDED) {
        if (life.reason != LIFE_EXIT || life.code != 31) {
          putstr("SIGCHILDRESULT index=");
          puthex(i);
          putstr(" code=");
          puthex(life.code);
          putstr("\n");
          panic("signal child baseline");
        }
        if (i < 2 && life.waitref)
          retained |= 1U << i;
        if (i >= 2) {
          REQUIRE(!life.waitref && !life.instance && !life.group &&
                  life.exit_policy == (i == 3 ? LIFE_AUTOCHILD : LIFE_AUTOCHILD | LIFE_IGNORECHLD));
          automatic |= 1U << i;
        }
      }
    }
    REQUIRE(!lifestat(parent.life, &life));
    if (life.state == LIFE_ENDED) {
      REQUIRE(life.reason == LIFE_EXIT && life.code == 42 && trace[0] == 99);
      break;
    }
    REQUIRE(procwork(8) >= 0 && prepwork(8) >= 0);
  }
  REQUIRE(trace[3] == 2 && trace[4] == 4 && trace[5] == 1 &&
          trace[7] == ((u64) 1 << (SIGNAL_USR1 - 1)) && trace[23] == 1 && retained == 3 &&
          automatic == 60);
  REQUIRE(trace[16] == 2 && trace[17] == trace[8] && trace[18] == 0 && trace[21] == 31 &&
          trace[14] && trace[22] == trace[14]);
  while ((error = procclean(pid)) > 0) {
  }
  REQUIRE(!error && !lifecollect(parent.life, 1, &result) && result.code == 42);
  for (i = 0; i < 256; i++)
    REQUIRE(procwork(32) >= 0 && prepwork(32) >= 0);
  for (i = 0; i < 6; i++)
    REQUIRE(have[i] && lifestat(children[i].life, &life) == -ERR_STALE);
  REQUIRE(!setstat(set, &account) && !account.charged && !account.instances && !setclose(set));
  pgstat(&after);
  REQUIRE(before.allocated == after.allocated && before.free == after.free && !pgcheck() &&
          !acctcheck() && !sigcheck() && !lifecheck() && !groupcheck());
  putstr("SIGCONFIGCHECK children=");
  puthex(7);
  putstr(" retained=");
  puthex(3);
  putstr(" automatic=");
  puthex(4);
  putstr(" old_result=");
  puthex(trace[23]);
  putstr(" child_info=");
  puthex(trace[16]);
  putstr(" charged=");
  puthex(account.charged);
  putstr(" allocated=");
  puthex(after.allocated);
  putstr("\n");
}
