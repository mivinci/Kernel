#ifndef _BOOTCPU_H
#define _BOOTCPU_H
#include <dtb.h>
typedef struct Bootcpu  Bootcpu;
typedef struct Bootcpus Bootcpus;
struct Bootcpu
{
  u64 id;
  u32 state, intc;
};
struct Bootcpus
{
  u64     current;
  u32     count, method;
  Bootcpu cpu[DT_CPUS];
};
/* Firmware admits exactly one cold entry. Verify every other enabled CPU is
   stopped before allocator/device access; disabled firmware CPUs are excluded. */
int bootcpucheck(const Dtinfo *info, u64 current, Bootcpus *out);
#endif
