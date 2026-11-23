#include "ktest.h"
#include <local.h>
#include <cow.h>
#include <diag.h>
#include <vm.h>
static Local  entries[2][LOCALS];
static Rtable tables[2];
#define REQUIRE(c)                                                                                 \
  do {                                                                                             \
    if (!(c)) {                                                                                    \
      putstr("LOCALFORKFAIL line=");                                                               \
      puthex(__LINE__);                                                                            \
      putstr("\n");                                                                                \
      panic("fork local inheritance");                                                             \
    }                                                                                              \
  } while (0)
static Memid
memory(Handle handle)
{
  const Local *slot = &entries[0][handleindex(handle)];
  Memid        result;
  result.generation = slot->objectgen;
  result.index = slot->object;
  result.reserved = 0;
  return result;
}
void
localforktest(void)
{
  static u64   generations[2][LOCALS];
  Setid        set;
  Set          group;
  Acctid       a, b;
  Spaceid      sa, sb;
  Lifeid       lives[2];
  Imageid      images[2], resolved;
  Life         state;
  Liferesult   result;
  Createarg    limits;
  Createpolicy policy, childpolicy;
  Forkbinding  bindings[4];
  Memid        source;
  Pgstat       before, after;
  Cowpool      pool;
  Allocarg     request;
  Handle space, allocator, private, alias, code, shared, unmapped, omitted, dead, control, call,
      send, create, newcap;
  Handle pids[2];
  Hpte   leaf;
  u64    value, bits;
  u32    i;
  pgstat(&before);
  REQUIRE(!setcreate(256, 512, 4, &set) && !acctcreate(set, 64, 128, &a) &&
          !acctcreate(set, 32, 64, &b));
  REQUIRE(!spacecreate(a, &sa) && !spacecreate(b, &sb));
  for (i = 0; i < 2; i++) {
    pids[i] = (2 + (500 + i) * 64 + (1 + i));
    REQUIRE(!lifecreaterun(set, pids[i], 1, i + 1, &lives[i]) && !imagecreate(lives[i], 0, &images[i]));
  }
  REQUIRE(!lifepublish(lives[0]) && !imageactivate(images[0]));
  for (i = 0; i < LOCALS; i++)
    entries[1][i].generation = 100 + i;
  REQUIRE(!localinit(&tables[0], entries[0], LOCALS, a, sa) &&
          !localinit(&tables[1], entries[1], LOCALS, b, sb));
  for (i = 0; i < 2; i++) {
    tables[i].owner = pids[i];
    tables[i].image = 1;
    tables[i].generations = generations[i];
  }
  REQUIRE(!localspace(&tables[0], 3, &space) && !localallocator(&tables[0], 7, &allocator));
  request.bytes = PAGE_SIZE;
  request.kind = ALLOC_DATA;
  request.align = request.minaddr = request.maxaddr = request.reserved = 0;
  REQUIRE(!localalloc(&tables[0], allocator, &request, &private));
  source = memory(private);
  value = 0x4141;
  REQUIRE(!localwrite(&tables[0], private, 0, &value, 8) &&
          !localmem(&tables[0], source, VM_READ, 0, PAGE_SIZE, LOCAL_FORK, &alias));
  REQUIRE(!localmap(&tables[0], space, private, 0, 0x10000, PAGE_SIZE, VM_READ | VM_WRITE) &&
          !localmap(&tables[0], space, alias, 0, 0x20000, PAGE_SIZE, VM_READ));
  request.kind = ALLOC_CODE;
  REQUIRE(!localalloc(&tables[0], allocator, &request, &code));
  value = 0x4343;
  REQUIRE(!localwrite(&tables[0], code, 0, &value, 8) && !localseal(&tables[0], code) &&
          !localmap(&tables[0], space, code, 0, 0x30000, PAGE_SIZE, VM_READ | VM_EXEC));
  request.kind = ALLOC_SHARED;
  REQUIRE(!localalloc(&tables[0], allocator, &request, &shared) &&
          !localmap(&tables[0], space, shared, 0, 0x40000, PAGE_SIZE, VM_READ | VM_WRITE));
  request.kind = ALLOC_DATA;
  REQUIRE(!localalloc(&tables[0], allocator, &request, &unmapped) &&
          !localalloc(&tables[0], allocator, &request, &omitted) &&
          !localinherit(&tables[0], omitted, 0) &&
          !localalloc(&tables[0], allocator, &request, &dead) && !localfree(&tables[0], dead));
  REQUIRE(!localcontrol(&tables[0], lives[0], CONTROL_QUERY, &control) &&
          !localendpoint(&tables[0], images[0], LOCAL_CALL, LOCAL_USE | LOCAL_GRANT, 0, LOCAL_FORK,
                         &call) &&
          !localendpoint(&tables[0], images[0], LOCAL_SEND, LOCAL_USE, 0, 0, &send));
  policy.set = set;
  policy.memlimit = 64;
  policy.exec_limit = 128;
  policy.resources = LOCALS;
  policy.pending = 8;
  policy.rights = CREATE_RIGHTS | LOCAL_GRANT;
  REQUIRE(!localcreate(&tables[0], &policy, &create));
  REQUIRE(!spacesnapshot(sa, sb));
  bindings[0].source = memory(private);
  bindings[1].source = memory(code);
  bindings[2].source = memory(shared);
  for (i = 0; i < 3; i++)
    REQUIRE(!spacesnapshotview(sa, sb, bindings[i].source, &bindings[i].target));
  bindings[3].source = memory(unmapped);
  REQUIRE(!memclone(bindings[3].source, b, &bindings[3].target));
  limits.memlimit = 32;
  limits.exec_limit = 64;
  limits.resources = LOCALS;
  limits.pending = 1;
  limits.ruid = limits.euid = limits.rgid = limits.egid = 0;
  limits.reserved = 0;
  generations[0][handleindex(omitted)] += 4;
  generations[0][handleindex(private)] += 4;
  REQUIRE(!localfork(&tables[0], &tables[1], bindings, 4, &limits));
  REQUIRE(!memfree(bindings[3].target));
  for (i = 0; i < LOCALS; i++)
    REQUIRE(generations[1][i] == generations[0][i] &&
            entries[1][i].generation ==
                (entries[1][i].subtype ? entries[0][i].generation : generations[0][i]));
  REQUIRE(!localnative(&tables[1], space) && !localnative(&tables[1], allocator) &&
          !localnative(&tables[1], private) && !localnative(&tables[1], alias) &&
          !localnative(&tables[1], code) && !localnative(&tables[1], shared) &&
          !localnative(&tables[1], unmapped) && !localnative(&tables[1], call) &&
          !localnative(&tables[1], create));
  REQUIRE(localnative(&tables[1], omitted) == -ERR_BAD_HANDLE &&
          localnative(&tables[1], dead) == -ERR_BAD_HANDLE &&
          localnative(&tables[1], control) == -ERR_BAD_HANDLE &&
          localnative(&tables[1], send) == -ERR_BAD_HANDLE);
  REQUIRE(entries[1][handleindex(space)].object == sb.index &&
          entries[1][handleindex(space)].objectgen == sb.generation &&
          entries[1][handleindex(allocator)].object == b.index &&
          entries[1][handleindex(allocator)].objectgen == b.generation &&
          entries[1][handleindex(private)].object == bindings[0].target.index &&
          entries[1][handleindex(alias)].object == bindings[0].target.index);
  REQUIRE(!localcreator(&tables[1], create, &childpolicy) && childpolicy.rights == CREATE_USE &&
          childpolicy.memlimit == 32 && childpolicy.exec_limit == 64 &&
          childpolicy.resources == LOCALS && childpolicy.pending == 1 &&
          childpolicy.set.index == set.index);
  REQUIRE(!localchannel(&tables[1], call, LOCAL_CALL, &resolved, &bits) &&
          resolved.generation == images[0].generation && resolved.index == images[0].index);
  REQUIRE(!localmap(&tables[1], space, private, 0, 0x50000, PAGE_SIZE, VM_READ | VM_WRITE));
  value = 0x6262;
  REQUIRE(!spacewrite(sb, 0x10000, &value, 8) && !spaceread(sb, 0x20000, &value, 8) &&
          value == 0x6262 && !spaceread(sb, 0x50000, &value, 8) && value == 0x6262 &&
          !spaceread(sa, 0x10000, &value, 8) && value == 0x4141);
  value = 0x7373;
  REQUIRE(!spacewrite(sb, 0x40000, &value, 8) && !spaceread(sa, 0x40000, &value, 8) &&
          value == 0x7373);
  REQUIRE(localmap(&tables[1], space, code, 0, 0x60000, PAGE_SIZE, VM_READ | VM_WRITE) ==
          -ERR_DENIED);
  value = 0x7777;
  REQUIRE(!localwrite(&tables[1], unmapped, 0, &value, 8));
  REQUIRE(!mempage(bindings[3].source, 0, &bits) && *(const u64 *) kaddr(bits) == 0);
  REQUIRE(!localalloc(&tables[1], allocator, &request, &newcap) && newcap != omitted &&
          handlegen(newcap) > generations[0][handleindex(omitted)] &&
          localnative(&tables[1], omitted) == -ERR_BAD_HANDLE);
  REQUIRE(!localfree(&tables[1], private) && !localnative(&tables[0], private) &&
          !spacelookup(sb, 0x60000, &leaf) &&
          !localalloc(&tables[1], allocator, &request, &newcap) &&
          handleindex(newcap) == handleindex(private) &&
          handlegen(newcap) > generations[0][handleindex(private)]);
  for (i = 0; i < 2; i++) {
    REQUIRE(!localclose(&tables[i]));
    while (localdrain(&tables[i], 32) > 0) {
    }
  }
  while (spaceclear(sa, 32) > 0) {
  }
  while (spaceclear(sb, 32) > 0) {
  }
  REQUIRE(!spacedestroy(sa) && !spacedestroy(sb));
  for (i = 0; i < 2; i++) {
    REQUIRE(!imageend(images[i], i ? IMAGE_ABORT : IMAGE_EXIT) && !imagedetach(images[i]));
    REQUIRE(!(i ? lifecancel(lives[i]) : lifeend(lives[i], LIFE_EXIT, 42, NULL)) &&
            !lifedetach(lives[i]));
    if (!lifestat(lives[i], &state) && state.waitref)
      REQUIRE(!lifecollect(lives[i], 1, &result));
  }
  REQUIRE(!acctclose(a) && !acctunbind(a) && !acctclose(b) && !acctunbind(b));
  cowpool(&pool);
  REQUIRE(!pool.branches && !pool.groups && !pool.prepaid && !pgreserved() &&
          !setstat(set, &group) && !group.charged && !group.instances && !setclose(set) &&
          !acctcheck() && !pgcheck() && !cowcheck() && !lifecheck());
  pgstat(&after);
  REQUIRE(before.free == after.free && before.allocated == after.allocated);
  /* Exact inherited entries, omitted tombstones, private alias and CREATE attenuation. */
  putstr("LOCALFORKCHECK inherited=");
  puthex(9);
  putstr(" omitted=");
  puthex(3);
  putstr(" aliases=");
  puthex(2);
  putstr(" attenuated=");
  puthex(1);
  putstr(" charged=");
  puthex(0);
  putstr(" allocated=");
  puthex(after.allocated);
  putstr("\n");
}
