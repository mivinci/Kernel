#include "io.h"

#define BLK_SLOT_BITS 7
#define BLK_SLOT_MASK ((1U << BLK_SLOT_BITS) - 1)
STATIC_ASSERT(VQ_SIZE <= (1U << BLK_SLOT_BITS), blk_cookie_slot);

int
blkiosubmit(Vblkreq *request, Vring *ring, const Vblkconfig *config, const Blkdma *dma, u32 type,
            u64 sector, u32 bytes, u64 cookie, const void *source, Vblksync sync, void *context)
{
  const u8 *input = source;
  u32       i;
  if (!request || !dma || !dma->header || !dma->status || bytes > dma->capacity ||
      (bytes && !dma->data) || ((type == VBLK_WRITE) != (source != NULL)))
    return -ERR_INVALID;
  if (request->held || request->active)
    return -ERR_BUSY;
  if (type == VBLK_WRITE)
    for (i = 0; i < bytes; i++)
      dma->data[i] = input[i];
  return vblksubmit(request, ring, config, type, sector, cookie, dma->headaddr,
                    bytes ? dma->dataaddr : 0, bytes, dma->statusaddr, dma->header, dma->status,
                    sync, context);
}

int
blkiofinish(Vblkreq *request, const Vdone *done, const Blkdma *dma, void *destination, u32 capacity)
{
  u8 *output = destination;
  u32 i;
  int result;
  if (!request || !done || !dma || request->status != dma->status)
    return -ERR_INVALID;
  if (output && (request->type != VBLK_READ || request->buffers[1].address != dma->dataaddr ||
                 !dma->data || request->bytes > dma->capacity || request->bytes > capacity))
    return -ERR_INVALID;
  if (!output && capacity)
    return -ERR_INVALID;
  result = vblkfinish(request, done);
  if (result == VBLK_OK && output)
    for (i = 0; i < request->bytes; i++)
      output[i] = dma->data[i];
  return result;
}

int
blkirqpoll(Vdev *device, Vring *ring, u64 generation, Vdone *done, u32 *count, u32 *more,
           u32 *causes)
{
  int error;
  if (!done || !count || !more || !causes)
    return -ERR_INVALID;
  *count = *more = *causes = 0;
  if (!ring || !ring->initialized)
    return -ERR_INVALID;
  if (generation != ring->queue.generation)
    return -ERR_STALE;
  error = vinterrupt(device, causes);
  return error ? error : vrpoll(ring, generation, done, count, more);
}

static int
windows(const Blkdma *buffer, u32 count, const u64 pages[3], u32 cpu)
{
  u32 i, j, k, l;
  for (i = 0; i < 3; i++) {
    if (!IS_ALIGNED(pages[i], PAGE_SIZE) || PAGE_SIZE > UINT64_MAX - pages[i])
      return 0;
    for (j = 0; j < i; j++)
      if (pages[j] == pages[i])
        return 0;
  }
  for (i = 0; i < count; i++) {
    const Blkdma *a = &buffer[i];
    const u64     base[] = {cpu ? (uintptr_t) a->header : a->headaddr,
                        cpu ? (uintptr_t) a->data : a->dataaddr,
                        cpu ? (uintptr_t) a->status : a->statusaddr};
    const u64 size[] = {sizeof(*a->header), a->capacity, 1};
    if (!a->header || !a->status || (a->capacity && !a->data))
      return 0;
    for (k = 0; k < ARRAY_SIZE(base); k++) {
      if (size[k] > UINT64_MAX - base[k])
        return 0;
      if (!size[k])
        continue;
      for (j = 0; j < 3; j++)
        if (base[k] < pages[j] + PAGE_SIZE && pages[j] < base[k] + size[k])
          return 0;
      for (j = 0; j <= i; j++) {
        const Blkdma *b = &buffer[j];
        const u64     other[] = {cpu ? (uintptr_t) b->header : b->headaddr,
                             cpu ? (uintptr_t) b->data : b->dataaddr,
                             cpu ? (uintptr_t) b->status : b->statusaddr};
        const u64 length[] = {sizeof(*b->header), b->capacity, 1};
        for (l = 0; l < (i == j ? k : ARRAY_SIZE(other)); l++)
          if (length[l] && base[k] < other[l] + length[l] && other[l] < base[k] + size[k])
            return 0;
      }
    }
  }
  return 1;
}

static int
checkring(const Blkqueue *queue, const Vdev *device, u64 generation, const Vblkreq *slots,
          const Blkdma *buffers, u32 count, const u64 addresses[3], const u64 pages[3])
{
  if (!queue || !device || !generation || !slots || !buffers || !count || count > VQ_SIZE ||
      !addresses)
    return -ERR_INVALID;
  if (queue->state && (queue->state != BLKQ_STOPPED || device != queue->device))
    return -ERR_STATE;
  if (queue->state && generation <= queue->generation)
    return -ERR_STALE;
  if (!windows(buffers, count, addresses, 0) || !windows(buffers, count, pages, 1))
    return -ERR_INVALID;
  return 0;
}

static void
bindring(Blkqueue *queue, Vdev *device, Vring *ring, Vblkreq *slots, const Blkdma *buffers,
         u32 count, const Vblkconfig *config)
{
  u32 i;
  queue->config = *config;
  queue->device = device;
  queue->ring = ring;
  queue->slots = slots;
  queue->buffers = buffers;
  queue->count = count;
  queue->generation = ring->queue.generation;
  queue->geometry = 1;
  queue->serial = 0;
  queue->reclaimed = 0;
  queue->ringreclaimed = 0;
  queue->configpending = 0;
  queue->state = BLKQ_OPEN;
  for (i = 0; i < count; i++)
    slots[i].held = slots[i].active = 0;
}

int
blkqinit(Blkqueue *queue, Vdev *device, Vring *ring, Vblkreq *slots, const Blkdma *buffers,
         u32 count, const u64 addresses[3])
{
  u64        pages[3];
  Vblkconfig config;
  int        error;
  if (!ring || !ring->initialized)
    return -ERR_INVALID;
  pages[0] = (uintptr_t) ring->desc;
  pages[1] = (uintptr_t) ring->avail;
  pages[2] = (uintptr_t) ring->used;
  error = checkring(queue, device, ring->queue.generation, slots, buffers, count, addresses, pages);
  if (error)
    return error;
  if (device->state != VDEV_LIVE)
    return -ERR_STATE;
  error = vblkconfig(device, &config);
  if (error)
    return error;
  bindring(queue, device, ring, slots, buffers, count, &config);
  return 0;
}

int
blkqstart(Blkqueue *queue, Vdev *device, Vring *ring, u64 generation, void *const mapping[3],
          const u64 addresses[3], const Vringops *ops, Vblkreq *slots, const Blkdma *buffers,
          u32 count)
{
  u64        pages[3];
  Vblkconfig config;
  int        error;
  if (!ring || !mapping || !mapping[0] || !mapping[1] || !mapping[2])
    return -ERR_INVALID;
  pages[0] = (uintptr_t) mapping[0];
  pages[1] = (uintptr_t) mapping[1];
  pages[2] = (uintptr_t) mapping[2];
  error = checkring(queue, device, generation, slots, buffers, count, addresses, pages);
  if (error)
    return error;
  if (device->state != VDEV_FEATURES || device->queues)
    return -ERR_STATE;
  error = vblkconfig(device, &config);
  if (!error)
    error = vrinit(ring, generation, mapping[0], mapping[1], mapping[2], ops);
  if (error)
    return error;
  bindring(queue, device, ring, slots, buffers, count, &config);
  queue->state = BLKQ_FAULT;
  error = vqueue(device, 0, addresses[0], addresses[1], addresses[2]);
  if (!error)
    error = vstart(device);
  if (error)
    return error;
  queue->state = BLKQ_OPEN;
  return 0;
}

int
blkqpoll(Blkqueue *queue, Vdone *done, u32 *count, u32 *more, u32 *causes)
{
  int error;
  if (!queue || !done || !count || !more || !causes)
    return -ERR_INVALID;
  if (queue->state != BLKQ_OPEN) {
    *count = *more = *causes = 0;
    return -ERR_STATE;
  }
  error = blkirqpoll(queue->device, queue->ring, queue->generation, done, count, more, causes);
  if (error) {
    queue->state = BLKQ_FAULT;
    return error;
  }
  if (*causes & VDEV_IRQ_CONFIG)
    queue->configpending = 1;
  return 0;
}

int
blkqrefresh(Blkqueue *queue, Vblkconfig *config)
{
  u32 i;
  int error;
  if (!queue || !config)
    return -ERR_INVALID;
  if (queue->state != BLKQ_OPEN || queue->device->state != VDEV_LIVE)
    return -ERR_STATE;
  if (queue->ring->queue.generation != queue->generation)
    return -ERR_STALE;
  if (queue->configpending) {
    if (queue->geometry == UINT64_MAX)
      return -ERR_OVERFLOW;
    for (i = 0; i < queue->count; i++)
      if (queue->slots[i].active || queue->slots[i].held)
        return -ERR_WOULD_BLOCK;
    error = vblkconfig(queue->device, &queue->config);
    if (error)
      return error;
    queue->configpending = 0;
    queue->geometry++;
  }
  *config = queue->config;
  return 0;
}

int
blkqsubmit(Blkqueue *queue, u32 index, u32 type, u64 sector, u32 bytes, const void *source,
           Vblksync sync, void *context)
{
  Vblkreq *slot;
  u64      cookie;
  u32      i;
  int      error;
  if (!queue || index >= queue->count)
    return -ERR_INVALID;
  if (queue->state != BLKQ_OPEN || queue->device->state != VDEV_LIVE)
    return -ERR_STATE;
  if (queue->ring->queue.generation != queue->generation)
    return -ERR_STALE;
  if (queue->configpending)
    return -ERR_WOULD_BLOCK;
  slot = &queue->slots[index];
  if (slot->held || slot->active)
    return -ERR_BUSY;
  if (type == VBLK_FLUSH || type == VBLK_WRITE)
    for (i = 0; i < queue->count; i++) {
      const Vblkreq *pending = &queue->slots[i];
      if (pending->active &&
          (pending->type == VBLK_FLUSH || (type == VBLK_FLUSH && pending->type == VBLK_WRITE)))
        return -ERR_WOULD_BLOCK;
    }
  if (queue->serial == (UINT64_MAX >> BLK_SLOT_BITS))
    return -ERR_OVERFLOW;
  cookie = (++queue->serial << BLK_SLOT_BITS) | index;
  error = blkiosubmit(slot, queue->ring, &queue->config, &queue->buffers[index], type, sector,
                      bytes, cookie, source, sync, context);
  if (error && (slot->held || queue->ring->queue.broken))
    queue->state = BLKQ_FAULT;
  return error;
}

int
blkqsubmitbytes(Blkqueue *queue, u32 slot, u32 type, u64 offset, u32 bytes, const void *source,
                Vblksync sync, void *context)
{
  if (offset % VBLK_SECTOR || (type == VBLK_FLUSH && offset))
    return -ERR_INVALID;
  return blkqsubmit(queue, slot, type, offset / VBLK_SECTOR, bytes, source, sync, context);
}

int
blkqmatch(const Blkqueue *queue, const Vdone *done)
{
  const Vblkreq *slot;
  u32            index;
  if (!queue || !done)
    return -ERR_INVALID;
  if (queue->state != BLKQ_OPEN)
    return -ERR_STATE;
  index = (u32) (done->cookie & BLK_SLOT_MASK);
  if (index >= queue->count || done->generation != queue->generation ||
      queue->ring->queue.generation != queue->generation)
    return -ERR_STALE;
  slot = &queue->slots[index];
  if (!slot->active || done->cookie != slot->cookie || done->generation != slot->generation)
    return -ERR_STALE;
  return (int) index;
}

int
blkqfinish(Blkqueue *queue, const Vdone *done, void *destination, u32 capacity)
{
  int      index = blkqmatch(queue, done), result;
  Vblkreq *slot;
  if (index < 0)
    return index;
  slot = &queue->slots[index];
  result = blkiofinish(slot, done, &queue->buffers[index], destination, capacity);
  if (result == -ERR_PEER || (!slot->active && slot->held))
    queue->state = BLKQ_FAULT;
  return result;
}

int
blkqrecover(Blkqueue *queue, u64 now)
{
  u32 budget;
  int error;
  if (!queue || !queue->state)
    return -ERR_INVALID;
  if (queue->state == BLKQ_STOPPED)
    return 0;
  if (queue->state != BLKQ_RECLAIM) {
    queue->state = BLKQ_STOPPING;
    error = queue->device->state == VDEV_RESETTING ? vresetpoll(queue->device, now)
                                                   : vresetbegin(queue->device, now);
    if (error)
      return error;
    queue->state = BLKQ_RECLAIM;
  }
  for (budget = VQ_BATCH; budget && queue->reclaimed < queue->count; budget--) {
    error = vblkreclaim(&queue->slots[queue->reclaimed]);
    if (error)
      return error;
    queue->reclaimed++;
  }
  if (queue->reclaimed < queue->count)
    return -ERR_WOULD_BLOCK;
  error = vrreclaim(queue->ring, &queue->ringreclaimed);
  if (error)
    return error;
  queue->state = BLKQ_STOPPED;
  return 0;
}
