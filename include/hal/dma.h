#ifndef _HAL_DMA_H
#define _HAL_DMA_H
#include <types.h>

#define HDMA_BEFORE       0
#define HDMA_AFTER        1
#define HDMA_DEVICE_READ  1
#define HDMA_DEVICE_WRITE 2

/* Noncoherent maintenance granule; zero means unsupported. */
u64 hal_dma_granule(void);
/* Trusted kernel VA of pinned cacheable RAM, with compatible aliases. The
   caller has checked the device domain, window, and complete granule ownership.
   Noncoherent ranges must be aligned to hal_dma_granule(); coherent is true
   only for RAM proven coherent with this device by trusted platform policy. */
int hal_dma_sync(void *address, u64 bytes, u32 phase, u32 direction, u32 coherent);
#endif
