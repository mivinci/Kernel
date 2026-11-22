#include "private/ring.h"
static int
ready(Vring *r)
{
  if (!r || !r->initialized)
    return -ERR_INVALID;
  return r->queue.broken ? -ERR_STATE : 0;
}
static int
sync(Vring *r, u32 region, u32 phase, u64 offset, u64 length)
{
  int error;
  error = r->ops.sync(r->ops.context, region, phase, offset, length);
  return error > 0 ? -ERR_STATE : error;
}
static int
fail(Vring *r, int error)
{
  r->queue.broken = 1;
  return error;
}
int
vrinit(Vring *r, u64 generation, void *desc, void *avail, void *used, const Vringops *ops)
{
  volatile u8 *page;
  u32          i, region;
  int          error;
  if (!r || !generation || !desc || !avail || !used || !ops || !ops->sync || !ops->fence ||
      !ops->notify || !IS_ALIGNED((usize) desc, PAGE_SIZE) ||
      !IS_ALIGNED((usize) avail, PAGE_SIZE) || !IS_ALIGNED((usize) used, PAGE_SIZE) ||
      desc == avail || desc == used || avail == used)
    return -ERR_INVALID;
  r->initialized = r->backlog = 0;
  r->desc = desc;
  r->avail = avail;
  r->used = used;
  r->ops.context = ops->context;
  r->ops.sync = ops->sync;
  r->ops.fence = ops->fence;
  r->ops.notify = ops->notify;
  r->nextavail = r->nextused = 0;
  error = vqinit(&r->queue, generation);
  if (error)
    return error;
  for (region = 0; region < 3; region++) {
    page = region == VR_DESC ? desc : region == VR_AVAIL ? avail : used;
    for (i = 0; i < PAGE_SIZE; i++)
      page[i] = 0;
    error = sync(r, region, VR_BEFORE, 0, PAGE_SIZE);
    if (error)
      return fail(r, error);
  }
  r->ops.fence(r->ops.context, VR_RELEASE);
  r->initialized = 1;
  return 0;
}

int
vrreclaim(Vring *r, u32 *next)
{
  int error;
  if (!r || !r->initialized || !next || *next > VR_USED + 1)
    return -ERR_INVALID;
  r->queue.broken = 1;
  while (*next <= VR_USED) {
    error = sync(r, *next, VR_AFTER, 0, PAGE_SIZE);
    if (error)
      return error;
    (*next)++;
  }
  r->ops.fence(r->ops.context, VR_ACQUIRE);
  return 0;
}

int
vrsubmit(Vring *r, const Vbuf *buffers, u32 count, u64 cookie, u32 *published)
{
  u64 total, writable;
  u32 i, head, index, position, writes, start, span, next;
  int error;
  if (!published)
    return -ERR_INVALID;
  *published = 0;
  error = ready(r);
  if (error)
    return error;
  if (r->backlog)
    return -ERR_WOULD_BLOCK;
  if (!buffers || !count || count > VQ_SIZE)
    return -ERR_INVALID;
  total = writable = 0;
  writes = 0;
  for (i = 0; i < count; i++) {
    if (!buffers[i].length || buffers[i].writable > 1 ||
        buffers[i].address > ~(u64) 0 - buffers[i].length || (writes && !buffers[i].writable))
      return -ERR_INVALID;
    writes |= buffers[i].writable;
    total += buffers[i].length;
    if (buffers[i].writable)
      writable += buffers[i].length;
  }
  if (total > UINT32_MAX)
    return -ERR_OVERFLOW;
  error = vqreserve(&r->queue, count, (u32) writable, cookie, &head);
  if (error)
    return error;
  index = start = head;
  span = 0;
  for (i = 0; i < count; i++) {
    r->desc[index].address = buffers[i].address;
    r->desc[index].length = buffers[i].length;
    r->desc[index].flags = (u16) ((i + 1 < count ? 1 : 0) | (buffers[i].writable ? 2 : 0));
    r->desc[index].next = i + 1 < count ? r->queue.slots[index].next : 0;
    next = r->queue.slots[index].next;
    span++;
    /* Coalesce adjacent selected descriptors; never maintain the used page. */
    if (i + 1 == count || next != index + 1) {
      error = sync(r, VR_DESC, VR_BEFORE, (u64) start * sizeof(Vdesc), (u64) span * sizeof(Vdesc));
      if (error)
        goto unpublished;
      start = next;
      span = 0;
    }
    index = next;
  }
  position = r->nextavail % VQ_SIZE;
  r->avail->ring[position] = (u16) head;
  error = sync(r, VR_AVAIL, VR_BEFORE, 4 + 2 * position, 2);
  if (error)
    goto unpublished;
  r->ops.fence(r->ops.context, VR_RELEASE);
  error = vqpublish(&r->queue, head);
  if (error)
    return fail(r, error);
  /* Even failed cache maintenance may expose this store. Never undo it. */
  *published = 1;
  r->nextavail++;
  r->avail->index = r->nextavail;
  error = sync(r, VR_AVAIL, VR_BEFORE, 2, 2);
  if (error)
    return fail(r, error);
  r->ops.fence(r->ops.context, VR_RELEASE);
  error = r->ops.notify(r->ops.context);
  return error ? fail(r, error > 0 ? -ERR_STATE : error) : 0;
unpublished:
  if (vqcancel(&r->queue, head))
    return fail(r, -ERR_STATE);
  return fail(r, error);
}
int
vrpoll(Vring *r, u64 generation, Vdone *out, u32 *count, u32 *more)
{
  Vused batch[VQ_BATCH];
  u32   i, delta, amount, position, first;
  u16   observed;
  int   error;
  if (!count || !more)
    return -ERR_INVALID;
  *count = *more = 0;
  error = ready(r);
  if (error)
    return error;
  if (!out)
    return -ERR_INVALID;
  if (generation != r->queue.generation)
    return -ERR_STALE;
  error = sync(r, VR_USED, VR_AFTER, 2, 2);
  if (error)
    return fail(r, error);
  observed = r->used->index;
  r->ops.fence(r->ops.context, VR_ACQUIRE);
  delta = (u16) (observed - r->nextused);
  if (delta > VQ_SIZE)
    return fail(r, -ERR_PEER);
  if (!delta) {
    r->backlog = 0;
    return 0;
  }
  amount = MIN(delta, VQ_BATCH);
  position = r->nextused % VQ_SIZE;
  first = MIN(amount, VQ_SIZE - position);
  error = sync(r, VR_USED, VR_AFTER, 4 + 8 * position, 8 * first);
  if (!error && first < amount)
    error = sync(r, VR_USED, VR_AFTER, 4, 8 * (amount - first));
  if (error)
    return fail(r, error);
  for (i = 0; i < amount; i++) {
    position = (r->nextused + i) % VQ_SIZE;
    batch[i].id = r->used->ring[position].id;
    batch[i].length = r->used->ring[position].length;
  }
  error = vqcomplete(&r->queue, generation, batch, amount, out);
  if (error)
    return error;
  r->nextused = (u16) (r->nextused + amount);
  *count = amount;
  *more = r->backlog = delta > amount;
  return 0;
}
