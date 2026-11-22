#ifndef _BLK_IO_H
#define _BLK_IO_H
#include "private/virtio/block.h"

typedef struct Blkdma   Blkdma;
typedef struct Blkqueue Blkqueue;
enum
{
  BLKQ_OPEN = 1,
  BLKQ_FAULT,
  BLKQ_STOPPING,
  BLKQ_RECLAIM,
  BLKQ_STOPPED
};
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
  u64           generation, geometry, serial;
  u32           count, state, reclaimed, ringreclaimed, configpending;
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
   stops polling the old ring when recovery starts. Keep buffers immutable.
   Pass the same device addresses (desc, avail, used) installed by vqueue.
   Request windows must be disjoint from each other and from those three
   ring pages in both address spaces. The caller owns the mappings. */
int blkqinit(Blkqueue *queue, Vdev *device, Vring *ring, Vblkreq *slots, const Blkdma *buffers,
             u32 count, const u64 addresses[3]);
/* Start queue zero on a reset, feature-negotiated block device. Validate all
   windows before initializing the ring or exposing any addresses to hardware.
   If device programming fails, the queue remains faulted and owns the DMA
   windows; drive blkqrecover before releasing them. */
int blkqstart(Blkqueue *, Vdev *, Vring *, u64 generation, void *const mapping[3],
              const u64 addresses[3], const Vringops *, Vblkreq *, const Blkdma *, u32 count);
/* Drain more before rearming IRQ. An observed CONFIG keeps submission
   blocked until refresh succeeds, even after the IRQ hint is cleared.
   Transport and buffer ownership faults close the queue until recovery. */
int blkqpoll(Blkqueue *queue, Vdone *done, u32 *count, u32 *more, u32 *causes);
/* A pending configuration takes effect only once old requests are drained.
   WOULD_BLOCK leaves the output and the admission gate unchanged. */
int blkqrefresh(Blkqueue *queue, Vblkconfig *config);
/* FLUSH waits for submitted writes to finish. While FLUSH is active, later
   writes and flushes wait. The caller orders requests before submission and
   handles completion failures before advancing a durability sequence. */
int blkqsubmit(Blkqueue *queue, u32 slot, u32 type, u64 sector, u32 bytes, const void *source,
               Vblksync sync, void *context);
/* CPU-side service boundary uses byte offsets. Convert only after checking
   representability; the queue checks the active device's logical geometry. */
int blkqsubmitbytes(Blkqueue *, u32 slot, u32 type, u64 offset, u32 bytes, const void *source,
                    Vblksync, void *context);
/* Return the exact active slot for a completion, or a native error. */
int blkqmatch(const Blkqueue *queue, const Vdone *done);
int blkqfinish(Blkqueue *queue, const Vdone *done, void *destination, u32 capacity);
/* Close admission and advance reset/reclaim by one step. WOULD_BLOCK means
   poll again. Other failures leave admission closed and retain progress;
   retry reset only within the device attempt limit. Zero means all old
   windows were returned. Old requests must be reported as failed. */
int blkqrecover(Blkqueue *queue, u64 now);
#endif
