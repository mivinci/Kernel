#include "ktest.h"
#include <bootcpu.h>
#include <clock.h>
#include <page.h>
#include <diag.h>
#define REQUIRE(c)                                                                                 \
  do {                                                                                             \
    if (!(c))                                                                                      \
      panic("CPU power cycle");                                                                    \
  } while (0)
static void
field(const char *name, u64 value)
{
  putstr(name);
  puthex(value);
}
void
cputest(const Dtinfo *info, u64 current)
{
  Bootcpus snapshot;
  Pgstat   before, after;
  u64      begin, now;
  u32      i, state, cycles = 0;
  REQUIRE(info && info->cpus > 1 && !bootcpucheck(info, current, &snapshot));
  pgstat(&before);
  for (i = 0; i < info->cpus; i++) {
    if (info->cpu[i].id == current)
      continue;
    REQUIRE(!testcpustart(info->cpu[i].method, info->cpu[i].id) && !clockmono(&begin));
    for (;;) {
      REQUIRE(!hal_cpu_status(info->cpu[i].method, info->cpu[i].id, &state));
      if (state == CPU_STOPPED)
        break;
      REQUIRE(!clockmono(&now) && now - begin < 1000000000UL);
    }
    cycles++;
    field("CPU_CYCLE id=", info->cpu[i].id);
    field(" off=", 1);
    putstr("\n");
  }
  REQUIRE(!bootcpucheck(info, current, &snapshot) && !pgcheck());
  pgstat(&after);
  REQUIRE(after.free == before.free && after.allocated == before.allocated &&
          after.managed == before.managed);
  for (i = 0; i <= PG_ORDER; i++)
    REQUIRE(after.blocks[i] == before.blocks[i]);
  field("CPU_STOPCHECK count=", info->cpus);
  field(" cycled=", cycles);
  field(" off=", snapshot.count - 1);
  field(" allocated=", after.allocated);
  field(" free=", after.free);
  putstr("\n");
}
