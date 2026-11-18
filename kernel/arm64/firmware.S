/* Firmware calls and the stackless secondary power-down target. */
#include <hal/cpu.h>
.section .text,"ax"
.global hal_arm_psci
hal_arm_psci:
 mov w5, w0
 mov x0, x1
 mov x1, x2
 mov x2, x3
 mov x3, x4
 cmp w5, #CPU_PSCI_SMC
 b.eq 1f
 cmp w5, #CPU_PSCI_HVC
 b.eq 2f
 mov x0, #-1
 ret
1:
 smc #0
 ret
2:
 hvc #0
 ret

// PSCI CPU_ON context is the conduit. No shared stack or BSS is touched.
.global hal_cpu_stop_entry
hal_cpu_stop_entry:
 msr daifset, #15
 mov w5, w0
 ldr x0, =0x84000002
 mov x1, xzr
 mov x2, xzr
 mov x3, xzr
 cmp w5, #CPU_PSCI_SMC
 b.eq 3f
 cmp w5, #CPU_PSCI_HVC
 b.ne 5f
 hvc #0
 b 5f
3:
 smc #0
5:
 wfi
 b 5b
