#ifndef _BLK_IO_H
#define _BLK_IO_H
#include "private/virtio/block.h"

typedef struct Blkdma Blkdma;
struct Blkdma
{
  Vblkhead    *header;
  volatile u8 *data, *status;
  u64          headaddr, dataaddr, statusaddr;
  u32          capacity;
};

/* The driver owns the request and its exclusive DMA windows until completion
   or recovery. Write source and read destination are independent CPU buffers.
   An error with held windows leaves recovery to vblkreclaim after real stop. */
int blkiosubmit(Vblkreq *request, Vring *ring, const Vblkconfig *config, const Blkdma *dma,
                u32 type, u64 sector, u32 bytes, u64 cookie, const void *source, Vblksync sync,
                void *context);
/* Accept only a completion from this ring. For READ, copy only a successful
   result after AFTER maintenance. Passing NULL with zero capacity discards
   the result, for example when the original client has cancelled. */
int blkiofinish(Vblkreq *request, const Vdone *done, const Blkdma *dma, void *destination,
                u32 capacity);
#endif
