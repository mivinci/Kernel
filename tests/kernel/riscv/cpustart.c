#include "ktest.h"
#include <hal/cpu.h>
#include <riscv/firmware.h>
#include <vm.h>
int
testcpustart(u32 method, u64 id)
{
  Sbiret result;
  if (method != CPU_SBI_HSM)
    return -1;
  result = hal_sbi_call(0x48534d, 0, id, kphysical(hal_cpu_stop_entry), 0);
  return result.error ? -1 : 0;
}
