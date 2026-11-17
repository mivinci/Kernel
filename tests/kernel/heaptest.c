#include "ktest.h"
#include "load.h"
#include <proc.h>
#include <diag.h>
#include <abi/boot.h>
extern const u8  heap_image[], heap_image_end[];
extern const u64 heap_trace_address;
#define REQUIRE(c)                                                                                 \
  do {                                                                                             \
    if (!(c)) {                                                                                    \
      putstr("HEAPFAIL line=");                                                                    \
      puthex(__LINE__);                                                                            \
      putstr("\n");                                                                                \
      panic("libc heap");                                                                          \
    }                                                                                              \
  } while (0)
static u32
resources(Rtable *table)
{
  u32 i, used = 0;
  for (i = 0; i < table->count; i++)
    if (table->slots[i].subtype)
      used++;
  return used;
}
void
heaptest(void)
{
  struct
  {
    Boot     header;
    Resentry entries[2];
  } packet;
  Setid      set;
  Set        account;
  Procinfo   process;
  Rtable    *table;
  Acct       baseline, current;
  Life       life;
  Liferesult result;
  Pgstat     before, after;
  Hpte       entry;
  Handle     pid;
  u64        trace[32], tiny, big;
  u8         byte, data[32];
  u32        i, mode, small = 0, reclaimed = 0;
  int        error;
  pgstat(&before);
  REQUIRE(!setcreate(4096, 8192, 8, &set));
  for (mode = 0; mode < 2; mode++) {
    REQUIRE(!procnew(set, 1, 256, 512, LOCALS, &pid) && !procinfo(pid, &process));
    table = proctable(pid);
    REQUIRE(table);
    for (i = 0; i < sizeof(packet); i++)
      ((u8 *) &packet)[i] = 0;
    packet.header.magic = BOOT_MAGIC;
    packet.header.format = BOOT_FORMAT;
    packet.header.bytes = sizeof(packet);
    packet.header.resource_count = 2;
    packet.header.resources_off = sizeof(Boot);
    packet.header.user_lo = BOOT_USER_LO;
    packet.header.user_hi = BOOT_USER_HI;
    packet.entries[0].tag = BOOT_SELF_SPACE;
    packet.entries[0].handle = testresource(table, LOCAL_SPACE);
    packet.entries[1].tag = BOOT_SELF_ALLOC;
    packet.entries[1].handle = testresource(table, LOCAL_ALLOC);
    testload(pid, heap_image, (u64) (heap_image_end - heap_image), &packet, sizeof(packet));
    /* The shared trusted loader retains page managers until cleanup. Drop
       them after mapping, then fill unrelated fixed slots to leave one real
       temporary Mem slot for all subsequent native allocations. */
    for (i = 0; i < table->count; i++)
      if (table->slots[i].subtype == LOCAL_MEM)
        REQUIRE(!localfree(table, handlepack(HANDLE_RESOURCE, table->slots[i].generation, i)));
    while (resources(table) < table->count - 1) {
      Handle filler;
      REQUIRE(!localallocator(table, 7, &filler));
    }
    for (i = 0; i < 32; i++)
      trace[i] = 0;
    trace[1] = mode;
    REQUIRE(!spacewrite(process.space, heap_trace_address, trace, sizeof(trace)));
    REQUIRE(!acctstat(process.acct, &baseline) && resources(table) == LOCALS - 1);
    REQUIRE(!procpublish(pid) && !procrunone());
    REQUIRE(!spaceread(process.space, heap_trace_address, trace, sizeof(trace)));
    if (trace[31]) {
      putstr("HEAPUSER fail=");
      puthex(trace[31]);
      putstr("\n");
    }
    REQUIRE(trace[0] == 1 && !trace[31] && trace[4] == 64 && trace[7] == 1 && trace[12] == mode &&
            trace[11] == 128 * 1024 && !((trace[8] | trace[9]) & 15));
    big = trace[8];
    tiny = trace[9];
    REQUIRE(!procinfo(pid, &process) && process.state == PROC_READY &&
            resources(table) == LOCALS - 1);
    REQUIRE(!acctstat(process.acct, &current) && current.used >= baseline.used + 49);
    REQUIRE(spacelookup(process.space, ALIGN_DOWN(big, PAGE_SIZE), &entry) &&
            entry.prot == (VM_READ | VM_WRITE | VM_USER) && entry.attr == VM_NORMAL);
    REQUIRE(!spaceread(process.space, big, data, sizeof(data)));
    for (i = 0; i < sizeof(data); i++)
      REQUIRE(data[i] == 0x6d);
    REQUIRE(!spaceread(process.space, tiny, &byte, 1) && byte == 1);
    small += (u32) trace[4];
    REQUIRE(!procrunone());
    REQUIRE(!spaceread(process.space, heap_trace_address, trace, sizeof(trace)) && trace[0] == 2 &&
            !trace[31]);
    REQUIRE(!acctstat(process.acct, &current) && current.used == baseline.used &&
            current.reserved == baseline.reserved && resources(table) == LOCALS - 1 &&
            !spacelookup(process.space, ALIGN_DOWN(big, PAGE_SIZE), &entry));
    reclaimed++;
    REQUIRE(!procrunone() && !lifestat(process.life, &life) && life.state == LIFE_ENDED &&
            life.reason == LIFE_EXIT && life.code == 42);
    while ((error = procclean(pid)) > 0) {
    }
    REQUIRE(!error && !lifecollect(process.life, 1, &result));
  }
  REQUIRE(!setstat(set, &account) && !account.charged && !account.instances && !setclose(set));
  pgstat(&after);
  REQUIRE(before.allocated == after.allocated && before.free == after.free && !pgcheck() &&
          !acctcheck() && !lifecheck() && !groupcheck());
  putstr("HEAPCHECK cases=");
  puthex(2);
  putstr(" small=");
  puthex(small);
  putstr(" large=");
  puthex(2);
  putstr(" full=");
  puthex(2);
  putstr(" reclaimed=");
  puthex(reclaimed);
  putstr(" charged=");
  puthex(0);
  putstr(" allocated=");
  puthex(after.allocated);
  putstr("\n");
}
