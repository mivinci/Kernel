#ifndef _BLK_IO_H
#define _BLK_IO_H
#include "private/virtio/block.h"

typedef struct Blkdma   Blkdma;
typedef struct Blkqueue Blkqueue;
struct Blkdma
{
  Vblkhead    *header;
  volatile u8 *data, *status;
  u64          headaddr, dataaddr, statusaddr;
  u32          capacity;
};
struct Blkqueue
{
  Vdev         *device;
  Vring        *ring;
  Vblkreq      *slots;
  const Blkdma *buffers;
  Vblkconfig    config;
  u64           generation, serial;
  u32           count, state, reclaimed, configpending;
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
/* While the platform IRQ is masked, acknowledge device hints and poll one
   bounded completion batch. Poll even on an empty hint. */
int blkirqpoll(Vdev *device, Vring *ring, u64 generation, Vdone *done, u32 *count, u32 *more,
               u32 *causes);
/* Zero the queue before first use. Bind a live device and take ownership
   of its configured ring. Reinitialize only after recovery, with a newer
   generation on the same device. The event loop serializes operations and
   stops polling the old ring when recovery starts. Keep buffers immutable. */
int blkqinit(Blkqueue *queue, Vdev *device, Vring *ring, Vblkreq *slots, const Blkdma *buffers,
             u32 count);
/* Drain more before rearming IRQ. An observed CONFIG keeps submission
   blocked until refresh succeeds, even after the IRQ hint is cleared. */
int blkqpoll(Blkqueue *queue, Vdone *done, u32 *count, u32 *more, u32 *causes);
int blkqrefresh(Blkqueue *queue, Vblkconfig *config);
int blkqsubmit(Blkqueue *queue, u32 slot, u32 type, u64 sector, u32 bytes, const void *source,
               Vblksync sync, void *context);
/* Return the exact active slot for a completion, or a native error. */
int blkqmatch(const Blkqueue *queue, const Vdone *done);
int blkqfinish(Blkqueue *queue, const Vdone *done, void *destination, u32 capacity);
/* Close admission and advance reset/reclaim by one step. WOULD_BLOCK means
   poll again. Other failures leave admission closed and retain progress;
   retry reset only within the device attempt limit. Zero means all old
   windows were returned. Old requests must be reported as failed. */
int blkqrecover(Blkqueue *queue, u64 now);
#endif
