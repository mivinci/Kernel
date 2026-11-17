#include "ktest.h"
#include <space.h>
#include <vm.h>
#include <cow.h>
#include <diag.h>
#define REQUIRE(c)                                                                                 \
  do {                                                                                             \
    if (!(c)) {                                                                                    \
      putstr("FORKJOURNALFAIL line=");                                                             \
      puthex(__LINE__);                                                                            \
      putstr("\n");                                                                                \
      panic("durable fork backing journal");                                                       \
    }                                                                                              \
  } while (0)
static Memid
stage(Memid source, Acctid account)
{
  Memid view;
  REQUIRE(!memforkclone(source, account, &view));
  /* Only the held view escapes; this retired frame owns no undo list. */
  return view;
}
static u64
readword(Memid id)
{
  u64 pa;
  REQUIRE(!mempage(id, 0, &pa));
  return *(volatile u64 *) kaddr(pa);
}
void
forkjournaltest(void)
{
  Setid   set;
  Set     group;
  Acctid  a, b;
  Acct    account;
  Spaceid root;
  Memid   source, view, rejected;
  Mem     parent, child;
  Pgstat  before, after, staged;
  Cowpool pool;
  u64     value, pa;
  u32     mode, aborted = 0, committed = 0, retired = 0, failed = 0;
  pgstat(&before);
  for (mode = 0; mode < 4; mode++) {
    REQUIRE(!setcreate(128, 128, 4, &set) && !acctcreate(set, 32, 32, &a) &&
            !acctcreate(set, mode == 3 ? 1 : 32, mode == 3 ? 1 : 32, &b) &&
            !memcreate(a, 2 * PAGE_SIZE, &source));
    value = 0x4141;
    REQUIRE(!memwrite(source, 0, &value, sizeof(value)) && !mempage(source, 0, &pa));
    if (mode == 2)
      REQUIRE(!spacecreate(a, &root) &&
              !spacemap(root, source, 0, 0x10000, 2 * PAGE_SIZE, VM_READ) && !memfree(source));
    pgstat(&staged);
    if (mode == 3) {
      rejected.generation = 77;
      rejected.index = rejected.reserved = 77;
      REQUIRE(memforkclone(source, b, &rejected) == MEM_LIMIT && rejected.generation == 77 &&
              rejected.index == 77 && rejected.reserved == 77 && !memstat(source, &parent) &&
              !parent.fork && !parent.adopted && parent.refs == 1 && readword(source) == 0x4141);
      pgstat(&after);
      REQUIRE(after.allocated == staged.allocated);
      failed++;
    } else {
      view = stage(source, b);
      REQUIRE(!memstat(source, &parent) && parent.fork == MEM_FORK_SOURCE &&
              !memstat(view, &child) && child.fork == MEM_FORK_VIEW && child.adopted == 3 &&
              child.backing.generation == source.generation && child.backing.index == source.index);
      value = 0x4242;
      REQUIRE(memwrite(source, 0, &value, sizeof(value)) ==
                  (parent.managers ? MEM_BUSY : MEM_BAD) &&
              memcanmap(source, VM_READ) == (parent.managers ? MEM_BUSY : MEM_BAD) &&
              memforkcommit(source) == MEM_BAD);
      if (mode == 1) {
        REQUIRE(!memforkcommit(view) && !memstat(source, &parent) && !parent.fork &&
                parent.refs == 1 && !memstat(view, &child) && !child.fork && !child.adopted &&
                !child.backing.generation);
        REQUIRE(!memwrite(source, 0, &value, sizeof(value)) && readword(source) == value &&
                readword(view) == 0x4141 && !memfree(view));
        committed++;
      } else {
        REQUIRE(!memwrite(view, 0, &value, sizeof(value)) && readword(view) == value &&
                readword(source) == 0x4141);
        if (mode == 2) {
          REQUIRE(!spaceunmap(root, 0x10000) && !spacedestroy(root) && !acctclose(a) &&
                  !acctunbind(a) && !memstat(source, &parent) && !parent.managers &&
                  parent.refs == 1 && parent.fork == MEM_FORK_SOURCE);
          /* The final borrowed child reference, rather than a manager or C
             continuation, owns the last source lease and automatic undo. */
          REQUIRE(!memretain(view) && !memfree(view) && !memstat(view, &child) && !child.managers &&
                  child.refs == 1 && !memrelease(view) && memstat(source, &parent) == MEM_BAD &&
                  acctstat(a, &account) == AC_BAD);
          retired++;
        } else {
          REQUIRE(!memfree(view) && !memstat(source, &parent) && !parent.fork && !parent.adopted &&
                  parent.refs == 1 && !mempage(source, 0, &value) && value == pa &&
                  readword(source) == 0x4141);
          pgstat(&after);
          REQUIRE(after.allocated == staged.allocated);
          aborted++;
        }
      }
    }
    if (mode != 2)
      REQUIRE(!memfree(source) && !acctclose(a) && !acctunbind(a));
    REQUIRE(!acctclose(b) && !acctunbind(b) && !setstat(set, &group) && !group.charged &&
            !setclose(set));
    cowpool(&pool);
    REQUIRE(!pool.branches && !pool.groups && !pool.prepaid && !pgreserved() && !cowcheck() &&
            !acctcheck() && !pgcheck());
  }
  pgstat(&after);
  REQUIRE(before.allocated == after.allocated && before.free == after.free);
  putstr("FORKJOURNALCHECK cases=");
  puthex(4);
  putstr(" aborted=");
  puthex(aborted);
  putstr(" committed=");
  puthex(committed);
  putstr(" retired=");
  puthex(retired);
  putstr(" failed=");
  puthex(failed);
  putstr(" charged=");
  puthex(0);
  putstr(" allocated=");
  puthex(after.allocated);
  putstr("\n");
}
