#ifndef _RISCV_FIRMWARE_H
#define _RISCV_FIRMWARE_H
#include <types.h>
typedef struct Sbiret Sbiret;
struct Sbiret
{
  isize error;
  u64   value;
};
Sbiret hal_sbi_call(u64 extension, u64 function, u64 arg0, u64 arg1, u64 arg2);
#endif
