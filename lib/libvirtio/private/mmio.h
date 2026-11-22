#ifndef _VIRTIO_MMIO_H
#define _VIRTIO_MMIO_H
#include "device.h"

typedef struct Vmmio Vmmio;
struct Vmmio
{
  uintptr_t base;
  u64       bytes;
};

/* The caller holds the native mapping; this view covers only the exact
   device window, which may be smaller than the containing mapped page. */
int  vmmioinit(Vmmio *, Vdevops *, uintptr_t base, u64 bytes);
int  virtioread(void *, u32 offset, u32 *out);
void virtiofence(void *, u32 phase);
#endif
