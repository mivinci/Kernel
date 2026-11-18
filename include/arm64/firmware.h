#ifndef _ARM64_FIRMWARE_H
#define _ARM64_FIRMWARE_H
#include <types.h>
u64 hal_arm_psci(u32 method, u64 function, u64 arg0, u64 arg1, u64 arg2);
#endif
