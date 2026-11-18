#include <hal/cpu.h>
#include <riscv/firmware.h>
int
hal_cpu_probe(u32 method)
{
  Sbiret result;
  if (method != CPU_SBI_HSM)
    return -1;
  result = hal_sbi_call(0x10, 3, 0x48534d, 0, 0);
  return result.error || !result.value ? -1 : 0;
}
int
hal_cpu_status(u32 method, u64 id, u32 *state)
{
  Sbiret result;
  if (!state)
    return -1;
  *state = 0;
  if (method != CPU_SBI_HSM)
    return -1;
  result = hal_sbi_call(0x48534d, 2, id, 0, 0);
  if (result.error || result.value > 6)
    return -1;
  *state = result.value < 2 ? (u32) result.value : CPU_PENDING;
  return 0;
}
