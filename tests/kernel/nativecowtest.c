#include "ktest.h"
#include "load.h"
#include <proc.h>
#include <cow.h>
#include <abi/boot.h>
#include <diag.h>
#include <vm.h>
extern const u8  cow_user_image[], cow_user_image_end[];
extern const u64 cow_user_trace_address, cow_user_fault_address;
#define REQUIRE(c)                                                                                 \
  do {                                                                                             \
    if (!(c)) {                                                                                    \
      putstr("COWNATIVEFAIL line=");                                                               \
      puthex(__LINE__);                                                                            \
      putstr("\n");                                                                                \
      panic("user COW fault");                                                                     \
    }                                                                                              \
  } while (0)
static void
load(Handle pid, u64 role, Handle memory)
{
  struct
  {
    Boot     boot;
    Resentry entries[3];
  } packet;
  Procinfo process;
  u64      trace[32];
  u32      i;
  for (i = 0; i < sizeof(packet); i++)
    ((u8 *) &packet)[i] = 0;
  packet.boot.magic = BOOT_MAGIC;
  packet.boot.format = BOOT_FORMAT;
  packet.boot.bytes = sizeof(packet);
  packet.boot.resource_count = memory ? 3 : 2;
  packet.boot.resources_off = sizeof(Boot);
  packet.boot.user_lo = BOOT_USER_LO;
  packet.boot.user_hi = BOOT_USER_HI;
  packet.entries[0].tag = BOOT_SELF_SPACE;
  packet.entries[0].handle = testresource(proctable(pid), LOCAL_SPACE);
  packet.entries[1].tag = BOOT_SELF_ALLOC;
  packet.entries[1].handle = testresource(proctable(pid), LOCAL_ALLOC);
  if (memory) {
    packet.entries[2].tag = 257;
    packet.entries[2].handle = memory;
  }
  testload(pid, cow_user_image, (u64) (cow_user_image_end - cow_user_image), &packet,
           sizeof(packet));
  for (i = 0; i < 32; i++)
    trace[i] = 0;
  trace[1] = role;
  REQUIRE(!procinfo(pid, &process) &&
          !spacewrite(process.space, cow_user_trace_address, trace, sizeof(trace)));
}
static void
collect(Handle pid, Procinfo *process, u32 bad)
{
  Life       life;
  Liferesult result;
  int        error;
  REQUIRE(!lifestat(process->life, &life) && life.state == LIFE_ENDED);
  if (bad)
    REQUIRE(life.reason == LIFE_FAULT && life.code == SIGNAL_SEGV &&
            life.fault.pc == cow_user_fault_address && life.fault.address == 0x2200000);
  else
    REQUIRE(life.reason == LIFE_EXIT && life.code == 42);
  while ((error = procclean(pid)) > 0) {
  }
  REQUIRE(!error && !lifecollect(process->life, 1, &result));
}
static void
verify(Procinfo *process, u64 role, u32 writing)
{
  u64 trace[32], value;
  REQUIRE(!spaceread(process->space, cow_user_trace_address, trace, sizeof(trace)));
  if (trace[31]) {
    putstr("COWUSER code=");
    puthex(trace[31]);
    putstr(" role=");
    puthex(role);
    putstr("\n");
  }
  REQUIRE(!trace[31]);
  if (!writing)
    return;
  REQUIRE((trace[0] == 2 || trace[0] == 99) && trace[4] == 2 && trace[5] == 0x123400 + role &&
          trace[6] == 0x567800 + role);
  REQUIRE(!spaceread(process->space, 0x2000000, &value, 8) && value == 0x123400 + role);
  REQUIRE(!spaceread(process->space, 0x2201000, &value, 8) && value == 0x567800 + role);
}
void
nativecowtest(void)
{
  Setid         set;
  Set           group;
  Handle        parent, child, first, second, grant;
  Procinfo      a, b;
  Procinfo     *p, *q;
  Memid         original, copy;
  Life          life;
  Pgstat        before, after, pressure;
  Pgblk         raw;
  volatile u64 *memory;
  u64           chain, next;
  int           error;
  Hpte          entry;
  Cowpool       pool;
  u64           pattern, faults = proccowfaults(), pair_before, parent_role, trace[32];
  u32           mode, i, turns;
  pgstat(&before);
  for (mode = 0; mode < 5; mode++) {
    REQUIRE(!setcreate(4096, 8192, 8, &set) && !procnew(set, 1, 256, 512, LOCALS, &parent) &&
            !procnew(set, 1, 256, 512, LOCALS, &child) && !procinfo(parent, &a) &&
            !procinfo(child, &b));
    parent_role = mode == 2 ? 2 : mode == 3 ? 3 : mode == 4 ? 4 : 0;
    load(parent, parent_role, 0);
    REQUIRE(!memcreate(a.acct, 8192, &original));
    pattern = 0x41;
    REQUIRE(!memwrite(original, 0, &pattern, 8));
    pattern = 0x42;
    REQUIRE(!memwrite(original, 4096, &pattern, 8));
    REQUIRE(!spacemap(a.space, original, 0, 0x2000000, 8192, VM_READ | VM_WRITE) &&
            !spacemap(a.space, original, 0, 0x2100000, 8192, VM_READ | VM_WRITE) &&
            !spacemap(a.space, original, 0, 0x2200000, 8192, VM_READ));
    REQUIRE(!spaceclone(original, b.acct, &copy));
    grant = 0;
    if (!mode) {
      REQUIRE(!localmem(proctable(child), copy, VM_READ | VM_WRITE, 0, 8192, 0, &grant));
    }
    load(child, 1, grant);
    if (mode) {
      REQUIRE(!spacecowmap(b.space, copy, 0, 0x2000000, 8192) &&
              !spacecowmap(b.space, copy, 0, 0x2100000, 8192) &&
              !spacemap(b.space, copy, 0, 0x2200000, 8192, VM_READ));
    }
    REQUIRE(!memfree(original) && !memfree(copy));
    REQUIRE(spacelookup(a.space, 0x2000000, &entry) && entry.prot == (VM_READ | VM_USER | VM_COW));
    chain = 0;
    if (mode == 1 || mode == 4) {
      while (!(error = pgalloc(0, NULL, &raw))) {
        memory = kaddr(raw.pa);
        memory[0] = chain;
        chain = raw.pa;
      }
      pgstat(&pressure);
      REQUIRE(error == PG_NOMEM && pgreserved() == 2 && pressure.free == pressure.protected + 2);
    }
    pair_before = proccowfaults();
    if (mode == 2 || mode == 3) {
      REQUIRE(!procpublish(parent) && !procrunone());
      verify(&a, parent_role, 0);
      collect(parent, &a, mode == 3);
      REQUIRE(proccowfaults() == pair_before);
      REQUIRE(!procpublish(child) && !procrunone());
      verify(&b, 1, 1);
      REQUIRE(!procrunone());
      collect(child, &b, 0);
      REQUIRE(proccowfaults() - pair_before == 2);
    } else {
      first = mode ? parent : child;
      second = mode ? child : parent;
      p = mode ? &a : &b;
      q = mode ? &b : &a;
      REQUIRE(!procpublish(first) && !procrunone());
      verify(p, mode ? parent_role : 1, 1);
      if (!mode)
        REQUIRE(!spaceread(b.space, cow_user_trace_address, trace, sizeof(trace)) && trace[7] == 1);
      REQUIRE(!spaceread(q->space, 0x2000000, &pattern, 8) && pattern == 0x41 &&
              !spaceread(q->space, 0x2101000, &pattern, 8) && pattern == 0x42);
      REQUIRE(proccowfaults() - pair_before == (mode == 4 ? 0 : 2));
      if (mode == 1 || mode == 4) {
        pgstat(&pressure);
        REQUIRE(pressure.free == pressure.protected && !pgreserved());
      }
      REQUIRE(!procpublish(second));
      turns = 0;
      for (i = 0; i < 2; i++) {
        Procinfo *current = i ? &b : &a;
        for (;;) {
          REQUIRE(!lifestat(current->life, &life));
          if (life.state == LIFE_ENDED)
            break;
          REQUIRE(!procrunone() && ++turns < 16);
        }
      }
      verify(&a, parent_role, 1);
      verify(&b, 1, 1);
      REQUIRE(!spaceread(a.space, cow_user_trace_address, trace, sizeof(trace)) && trace[0] == 99);
      if (mode == 4)
        REQUIRE(trace[8] == 2);
      collect(parent, &a, 0);
      collect(child, &b, 0);
      REQUIRE(proccowfaults() - pair_before == (mode == 4 ? 2 : 4));
    }
    while (chain) {
      memory = kaddr(chain);
      next = memory[0];
      memory[0] = 0;
      raw.pa = chain;
      raw.order = raw.reserved = 0;
      REQUIRE(!pgfree(raw));
      chain = next;
    }
    cowpool(&pool);
    REQUIRE(!pool.branches && !pool.groups && !pool.prepaid && !cowcheck() && !pgreserved());
    REQUIRE(!setstat(set, &group) && !group.charged && !group.instances && !setclose(set) &&
            !acctcheck() && !pgcheck() && !lifecheck() && !groupcheck());
  }
  pgstat(&after);
  REQUIRE(before.free == after.free && before.allocated == after.allocated &&
          proccowfaults() - faults == 14);
  putstr("COWNATIVECHECK cases=");
  puthex(5);
  putstr(" faults=");
  puthex(proccowfaults() - faults);
  putstr(" exited=");
  puthex(9);
  putstr(" readonly=");
  puthex(1);
  putstr(" native_map=");
  puthex(1);
  putstr(" exhausted=");
  puthex(2);
  putstr(" kernel_copy=");
  puthex(1);
  putstr(" charged=");
  puthex(0);
  putstr(" allocated=");
  puthex(after.allocated);
  putstr("\n");
}
