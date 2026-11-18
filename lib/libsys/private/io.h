#ifndef _LIBSYS_IO_H
#define _LIBSYS_IO_H
#include <types.h>
/* Caller owns a valid Device mapping and checks register bounds/alignment.
   Ordered against preceding/following memory and device accesses. These are
   userspace instructions, not syscalls, cache maintenance or DMA-stop proof. */
u32  ioread8(u64 address);
u32  ioread32(u64 address);
void iowrite8(u64 address, u32 value);
void iowrite32(u64 address, u32 value);
#endif
