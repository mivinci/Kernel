#include "ktest.h"
#include <acct.h>
#include <diag.h>
#include <vm.h>

#define REQUIRE(condition, reason)                                                                 \
  do {                                                                                             \
    if (!(condition))                                                                              \
      panic(reason);                                                                               \
  } while (0)

void
accttest(void)
{
  Setid         set;
  Acctid        a, b, c, d;
  Acctblk       old, peer, wrong, fresh, denied;
  Set           group;
  Acct          account;
  Pgstat        before, after;
  volatile u64 *memory;
  pgstat(&before);
  REQUIRE(!acctcheck(), "account initial");
  REQUIRE(!setcreate(64, 128, 2, &set) && !setenter(set), "set admission");
  REQUIRE(!acctcreate(set, 16, 32, &a) && !acctalloc(a, 3, NULL, &old), "account allocation");
  memory = kaddr(old.block.pa);
  *memory = 0x31415926;
  REQUIRE(!acctstat(a, &account) && account.used == 8 && !account.reserved && account.refs == 2,
          "account used/reference");
  denied.block.pa = 0x123;
  denied.acct.generation = 77;
  REQUIRE(acctalloc(a, 4, NULL, &denied) == AC_LIMIT && denied.block.pa == 0x123 &&
              denied.acct.generation == 77,
          "ordinary allocation borrowed peak");
  REQUIRE(!acctstat(a, &account) && account.used == 8 && !account.reserved && account.refs == 2,
          "failed admission changed account");
  REQUIRE(!setenter(set) && !acctcreate(set, 16, 32, &b) && !acctalloc(b, 3, NULL, &peer),
          "peer account");
  wrong = old;
  wrong.acct = b;
  REQUIRE(acctfree(wrong) == AC_BAD && pgfree(old.block) == PG_BAD && *memory == 0x31415926,
          "block owner mismatch");
  REQUIRE(!acctclose(a) && !acctunbind(a), "account close");
  REQUIRE(!acctstat(a, &account) && account.state == AC_CLOSING && account.used == 8 &&
              account.refs == 1,
          "closing account lost backing");
  REQUIRE(acctreserve(a, 1) == AC_BAD && setenter(set) == AC_LIMIT, "closed account or Life quota");
  REQUIRE(!setleave(set) && !setenter(set) && !acctcreate(set, 16, 32, &c) && c.index != a.index,
          "retained slot reused");
  REQUIRE(!acctfree(old) && acctstat(a, &account) == AC_BAD, "closing account reclaim");
  REQUIRE(!acctclose(c) && !acctunbind(c) && !setleave(set) && !setenter(set),
          "replacement admission");
  REQUIRE(!acctcreate(set, 16, 32, &d) && d.index == a.index && d.generation > a.generation &&
              !acctalloc(d, 3, NULL, &fresh),
          "generation reuse");
  REQUIRE(fresh.block.pa == old.block.pa && acctfree(old) == AC_BAD && !acctstat(d, &account) &&
              account.used == 8,
          "old reference freed new backing");
  REQUIRE(!acctfree(fresh) && !acctclose(d) && !acctunbind(d) && !setleave(set),
          "replacement cleanup");
  REQUIRE(!acctfree(peer) && !acctclose(b) && !acctunbind(b) && !setleave(set), "peer cleanup");
  REQUIRE(!acctcheck() && !pgcheck() && !setstat(set, &group), "account conservation");
  REQUIRE(group.charged == 0 && group.instances == 0 && group.refs == 1, "set conservation");
  pgstat(&after);
  REQUIRE(before.free == after.free && before.allocated == after.allocated,
          "physical account rollback");
  REQUIRE(!setclose(set), "set close");
  putstr("ACCOUNT charged=");
  puthex(group.charged);
  putstr(" instances=");
  puthex(group.instances);
  putstr(" refs=");
  puthex(group.refs);
  putstr(" free=");
  puthex(after.free);
  putstr(" allocated=");
  puthex(after.allocated);
  putstr("\n");
}
