#include "ktest.h"
#include <acct.h>
#include <diag.h>
#include <vm.h>

#define REQUIRE(condition, reason)                                                                 \
  do {                                                                                             \
    if (!(condition))                                                                              \
      panic(reason);                                                                               \
  } while (0)

static void
cowtest(void)
{
  Setid         set;
  Acctid        parent, child;
  Acctcow       credit;
  Acctblk       old, stale, fresh;
  Acct          account;
  Set           group;
  Pgstat        before, after;
  volatile u64 *original, *copy;
  u32           mode, i;
  pgstat(&before);
  for (mode = 0; mode < 4; mode++) {
    credit.acct.generation = credit.acct.index = credit.acct.reserved = 0;
    credit.physical.pages = 0;
    REQUIRE(!setcreate(2, 2, 2, &set) && !acctcreate(set, 1, 1, &parent) &&
                !acctcreate(set, 1, 1, &child),
            "COW account exact quota");
    REQUIRE(!acctalloc(parent, 0, NULL, &old), "COW original allocation");
    original = kaddr(old.block.pa);
    *original = 0x31415926;
    REQUIRE(!acctcowreserve(child, 1, &credit) && pgreserved() == 1 && !acctstat(child, &account) &&
                account.cow == 1 && account.reserved == 1 && !account.used,
            "COW quota and physical reservation");
    REQUIRE(acctconvert(child, 1) == AC_BAD && acctrelease(child, 0, 1) == AC_BAD,
            "ordinary operation consumed COW quota");
    if (mode == 0) {
      REQUIRE(!acctcowalloc(&credit, &fresh) && fresh.block.pa != old.block.pa &&
                  !credit.physical.pages && !credit.acct.generation && !pgreserved(),
              "COW child redemption");
      copy = kaddr(fresh.block.pa);
      *copy = 0x27182818;
      REQUIRE(*original == 0x31415926 && *copy == 0x27182818 && !acctstat(child, &account) &&
                  account.used == 1 && !account.reserved && !account.cow,
              "COW redeemed accounting and independent physical page");
      REQUIRE(!acctfree(fresh), "COW child page release");
    } else if (mode == 1) {
      stale = old;
      REQUIRE(!acctclose(parent) && !acctunbind(parent) && !acctcowmove(&old, &credit),
              "COW departing payer transfer");
      REQUIRE(old.block.pa == stale.block.pa && old.acct.generation == child.generation &&
                  old.acct.index == child.index && acctstat(parent, &account) == AC_BAD &&
                  acctfree(stale) == AC_BAD && *original == 0x31415926 && !pgreserved(),
              "COW stable old page and consumed payer reference");
    } else if (mode == 2) {
      REQUIRE(!acctclose(child) && !acctunbind(child) && !acctcowcancel(&credit, 1) &&
                  acctstat(child, &account) == AC_BAD && !pgreserved(),
              "COW closing reservation cancellation");
    }
    if (mode == 3) {
      for (i = 0; i < PAGE_SIZE / sizeof(u64); i++)
        original[i] = 0x31415926 ^ i;
      stale = old;
      REQUIRE(acctalloc(parent, 0, NULL, &fresh) == AC_LIMIT &&
                  !acctcowreplace(&old, &credit, &fresh),
              "COW payer replacement without spare quota");
      REQUIRE(old.block.pa == stale.block.pa && fresh.block.pa != old.block.pa &&
                  old.acct.index == child.index && fresh.acct.index == parent.index &&
                  acctfree(stale) == AC_BAD && !credit.physical.pages && !pgreserved(),
              "COW payer replacement ownership");
      copy = kaddr(fresh.block.pa);
      for (i = 0; i < PAGE_SIZE / sizeof(u64); i++)
        copy[i] = original[i];
      for (i = 0; i < PAGE_SIZE / sizeof(u64); i++) {
        REQUIRE(copy[i] == (0x31415926 ^ i), "COW payer copied bytes");
        copy[i] ^= 0xfeedbeef;
        REQUIRE(original[i] == (0x31415926 ^ i), "COW payer write isolation");
      }
      REQUIRE(!acctstat(parent, &account) && account.used == 1 && !account.reserved &&
                  !acctstat(child, &account) && account.used == 1 && !account.cow &&
                  !account.reserved && !setstat(set, &group) && group.charged == 2,
              "COW payer replacement charge conservation");
      REQUIRE(!acctfree(fresh), "COW payer fresh cleanup");
    }
    REQUIRE(!acctfree(old), "COW final backing release");
    if (mode != 1)
      REQUIRE(!acctclose(parent) && !acctunbind(parent), "COW parent cleanup");
    if (mode != 2)
      REQUIRE(!acctclose(child) && !acctunbind(child), "COW child cleanup");
    REQUIRE(!setstat(set, &group) && !group.charged && !setclose(set) && !acctcheck() && !pgcheck(),
            "COW account conservation");
  }
  pgstat(&after);
  REQUIRE(before.free == after.free && before.allocated == after.allocated && !pgreserved(),
          "COW physical conservation");
  putstr("COWACCTCHECK cases=");
  puthex(4);
  putstr(" redeemed=");
  puthex(1);
  putstr(" transferred=");
  puthex(1);
  putstr(" cancelled=");
  puthex(1);
  putstr(" replaced=");
  puthex(1);
  putstr(" charged=");
  puthex(0);
  putstr(" allocated=");
  puthex(after.allocated);
  putstr("\n");
}
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
  cowtest();
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
