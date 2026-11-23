#include "ktest.h"
#include <prep.h>
#include <proc.h>
#include <cow.h>
#include <diag.h>
#include <vm.h>
static Local  slots[64];
static Rtable source;
#define REQUIRE(c)                                                                                 \
  do {                                                                                             \
    if (!(c)) {                                                                                    \
      putstr("FORKCLEANFAIL line=");                                                               \
      puthex(__LINE__);                                                                            \
      putstr("\n");                                                                                \
      panic("fork cleanup transfer");                                                              \
    }                                                                                              \
  } while (0)
static void
handoff(Prepid prep)
{
  /* No caller-local reference or rollback list escapes this retired frame. */
  REQUIRE(!prepforkabandon(prep));
}
static Memid
memory(const Local *slot)
{
  Memid id;
  id.generation = slot->objectgen;
  id.index = slot->object;
  id.reserved = 0;
  return id;
}
void
forkcleantest(void)
{
  Setid      set;
  Set        group;
  Acctid     acct;
  Spaceid    root;
  Lifeid     life;
  Life       status;
  Imageid    image;
  Handle     parent, child, self, allocator, mapped, unmapped, endpoint;
  Procinfo   process, state;
  Rtable    *target;
  Memid      original, view, loose;
  Mem        backing;
  Prepid     prep;
  Prepinfo   record;
  Createarg  limits;
  Allocarg   args;
  Pgstat     before, after;
  Cowpool    pool;
  Liferesult result;
  Hpte       pte;
  u64        value = 0x4141;
  u32        mode, i, j, ready, work, total = 0, drafts = 0, loaded = 0, sourceclosed = 0;
  int        error;
  pgstat(&before);
  for (mode = 0; mode < 4; mode++) {
    parent = (2 + (9000 + mode) * 64 + (61));
    REQUIRE(!setcreate(512, 512, 8, &set) && !acctcreate(set, 128, 128, &acct) &&
            !spacecreate(acct, &root) && !lifecreaterun(set, parent, 1, 61, &life) &&
            !lifebindacct(life, acct) && !imagecreate(life, 0, &image) &&
            !localinit(&source, slots, 64, acct, root));
    source.owner = parent;
    source.image = 1;
    REQUIRE(!localspace(&source, 3, &self) && !localallocator(&source, 7, &allocator));
    args.bytes = 16 * PAGE_SIZE;
    args.kind = ALLOC_DATA;
    args.align = args.minaddr = args.maxaddr = args.reserved = 0;
    REQUIRE(!localalloc(&source, allocator, &args, &mapped));
    original = memory(&slots[handleindex(mapped)]);
    REQUIRE(!memwrite(original, 0, &value, sizeof(value)));
    for (i = 0; i < 3; i++)
      REQUIRE(!spacemap(root, original, 0, 0x10000 + (u64) i * 0x20000, 16 * PAGE_SIZE,
                        VM_READ | VM_WRITE));
    args.bytes = 2 * PAGE_SIZE;
    REQUIRE(!localalloc(&source, allocator, &args, &unmapped));
    loose = memory(&slots[handleindex(unmapped)]);
    REQUIRE(!lifepublish(life) && !imageactivate(image) &&
            !localendpoint(&source, image, LOCAL_CALL, LOCAL_USE, 0, LOCAL_FORK, &endpoint) &&
            !procnew(set, parent, 128, 128, 64, &child) && !procinfo(child, &process));
    target = proctable(child);
    REQUIRE(target != NULL);
    for (i = 0; i < target->count; i++)
      if (target->slots[i].subtype)
        REQUIRE(!localrevoke(target, handlepack(HANDLE_RESOURCE, target->slots[i].generation, i)));
    REQUIRE(!prepforkcreate(&source, target, process.life, process.identity, &prep) &&
            !spacesnapshotopen(root, process.space, prep));
    if (mode) {
      for (i = 0; i < 100; i++) {
        error = spacesnapshotbuild(root, process.space);
        REQUIRE(error == 0 || error == 1);
        if (mode == 1) {
          ready = 0;
          for (j = 0; j < 3; j++) {
            REQUIRE(spacelookup(root, 0x10000 + (u64) j * 0x20000, &pte));
            ready += (pte.prot & VM_WRITE) == 0;
          }
          if (ready && error == 1)
            break;
        } else if (!error)
          break;
      }
      REQUIRE(i < 100);
    }
    if (mode >= 2) {
      REQUIRE(!spacesnapshotview(root, process.space, original, &view) &&
              !localforkbind(&source, target, handleindex(mapped), view) &&
              !spacesnapshotclone(root, process.space, loose, &view) &&
              !localforkbind(&source, target, handleindex(unmapped), view) && !memfree(view));
      drafts++;
      if (mode == 3) {
        limits.memlimit = limits.exec_limit = 128;
        limits.resources = 64;
        limits.pending = 1;
        limits.ruid = limits.euid = limits.rgid = limits.egid = limits.reserved = 0;
        REQUIRE(!localforkload(&source, target, &limits) &&
                !localforkreserve(&source, target, process.identity, 2));
        loaded++;
      }
    }
    handoff(prep);
    REQUIRE(!prepstat(prep, &record) && record.state == PREP_ABORTED &&
            record.cloning == PREP_DRAIN && record.refs == 3 && !record.target &&
            prepforkabandon(prep) == -ERR_STATE && prepforkfinish(prep) == -ERR_STATE &&
            spacesnapshotbuild(root, process.space) < 0);
    REQUIRE(!procdestroy(child) && procclean(child) == 1);
    if (mode == 3) {
      REQUIRE(!localclose(&source));
      while ((error = localdrain(&source, 32)) > 0) {
      }
      REQUIRE(!error);
      sourceclosed++;
    }
    work = 0;
    for (i = 0; i < 1000; i++) {
      error = prepwork(32);
      REQUIRE(error >= 0);
      work += (u32) error;
      REQUIRE(!prepstat(prep, &record));
      if (!record.cloning)
        break;
      REQUIRE(procclean(child) == 1 && !procinfo(child, &state) && !state.phase);
    }
    total += work;
    REQUIRE(i < 1000 && work > 2 && record.refs == 1 && !spacefrozen(root) &&
            !spacefrozen(process.space) && target->state == LOCAL_DEAD &&
            !memstat(original, &backing) && !backing.fork &&
            !spacelookup(process.space, 0x10000, &pte));
    REQUIRE(!prepdrop(prep));
    for (i = 0; i < 64; i++) {
      error = procclean(child);
      REQUIRE(error >= 0);
      if (!error)
        break;
    }
    REQUIRE(i < 64 && procinfo(child, &state) == -ERR_STALE && !localclose(&source));
    while ((error = localdrain(&source, 32)) > 0) {
    }
    REQUIRE(!error);
    while ((error = spaceclear(root, 32)) > 0) {
    }
    REQUIRE(!error && !spacedestroy(root) && !acctclose(acct) && !acctunbind(acct) &&
            !imageend(image, IMAGE_EXIT) && !imagedetach(image) &&
            !lifeend(life, LIFE_EXIT, 42, NULL) && !lifedetach(life));
    if (!lifestat(life, &status) && status.waitref)
      REQUIRE(!lifecollect(life, status.parent, &result));
    cowpool(&pool);
    REQUIRE(!pool.groups && !pool.branches && !pool.prepaid && !pgreserved() && !cowcheck() &&
            !acctcheck() && !pgcheck() && !lifecheck() && !groupcheck() && !setstat(set, &group) &&
            !group.charged && !group.instances && !setclose(set));
  }
  pgstat(&after);
  REQUIRE(before.free == after.free && before.allocated == after.allocated);
  putstr("FORKCLEANCHECK cases=");
  puthex(4);
  putstr(" drafts=");
  puthex(drafts);
  putstr(" loaded=");
  puthex(loaded);
  putstr(" source_closed=");
  puthex(sourceclosed);
  putstr(" work=");
  puthex(total);
  putstr(" charged=");
  puthex(0);
  putstr(" allocated=");
  puthex(after.allocated);
  putstr("\n");
}
