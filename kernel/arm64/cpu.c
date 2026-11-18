#include <hal/cpu.h>
#include <arm64/firmware.h>
int
hal_cpu_probe(u32 method)
{
  u64 version;
  if (method != CPU_PSCI_SMC && method != CPU_PSCI_HVC)
    return -1;
  version = hal_arm_psci(method, 0x84000000, 0, 0, 0);
  return version >> 32 || (version & 0x80000000U) || version < 2 ? -1 : 0;
}
int
hal_cpu_status(u32 method, u64 id, u32 *state)
{
  u64 value;
  if (!state)
    return -1;
  *state = 0;
  if ((method != CPU_PSCI_SMC && method != CPU_PSCI_HVC) || id & ~0xff00ffffffUL)
    return -1;
  value = hal_arm_psci(method, 0xc4000004, id, 0, 0);
  if (value > 2)
    return -1;
  *state = (u32) value;
  return 0;
}
