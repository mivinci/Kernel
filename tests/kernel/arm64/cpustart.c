#include "ktest.h"
#include <hal/cpu.h>
#include <arm64/firmware.h>
#include <vm.h>
int
testcpustart(u32 method, u64 id)
{
  return hal_arm_psci(method, 0xc4000003, id, kphysical(hal_cpu_stop_entry), method) ? -1 : 0;
}
