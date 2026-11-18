#ifndef _HAL_CPU_H
#define _HAL_CPU_H
#define CPU_POWER_NONE 0
#define CPU_PSCI_SMC   1
#define CPU_PSCI_HVC   2
#define CPU_SBI_HSM    3
#define CPU_RUNNING    0
#define CPU_STOPPED    1
#define CPU_PENDING    2
#ifndef __ASSEMBLER__
#include <types.h>
/* Firmware backend, before VM/IRQ/device setup. Unknown or unavailable power
   methods fail. No remote stop is implied by a status query. */
int hal_cpu_probe(u32 method);
int hal_cpu_status(u32 method, u64 id, u32 *state);
/* Firmware entry address only, not a C call. No stack/BSS/MMIO access. */
extern const u8 hal_cpu_stop_entry[];
#endif
#endif
