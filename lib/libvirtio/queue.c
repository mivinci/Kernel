#include "private/queue.h"
int
vqinit(Vq *q, u64 generation)
{
  u32 i;
  if (!q || !generation)
    return -ERR_INVALID;
  for (i = 0; i < VQ_SIZE; i++) {
    q->slots[i].cookie = 0;
    q->slots[i].writable = 0;
    q->slots[i].next = (u16) (i + 1);
    q->slots[i].state = VQ_FREE;
  }
  q->generation = generation;
  q->free = VQ_SIZE;
  q->head = q->broken = q->reserved = 0;
  return 0;
}
static int
ready(Vq *q)
{
  if (!q || !q->generation)
    return -ERR_INVALID;
  return q->broken ? -ERR_STATE : 0;
}
int
vqreserve(Vq *q, u32 count, u32 writable, u64 cookie, u32 *out)
{
  u32 head, index, i;
  int error;
  if (!out)
    return -ERR_INVALID;
  *out = VQ_NONE;
  error = ready(q);
  if (error)
    return error;
  if (!count || count > VQ_SIZE)
    return -ERR_INVALID;
  if (count > q->free)
    return -ERR_WOULD_BLOCK;
  head = index = q->head;
  for (i = 0; i < count; i++) {
    q->slots[index].state = i ? VQ_MEMBER : VQ_RESERVED;
    if (i + 1 == count) {
      q->head = q->slots[index].next;
      q->slots[index].next = VQ_NONE;
    } else {
      index = q->slots[index].next;
    }
  }
  q->slots[head].writable = writable;
  q->slots[head].cookie = cookie;
  q->free -= count;
  *out = head;
  return 0;
}
int
vqpublish(Vq *q, u32 head)
{
  int error = ready(q);
  if (error)
    return error;
  if (head >= VQ_SIZE || q->slots[head].state != VQ_RESERVED)
    return -ERR_STATE;
  q->slots[head].state = VQ_PUBLISHED;
  return 0;
}
static void
release(Vq *q, u32 head)
{
  u32 index, next;
  index = head;
  do {
    next = q->slots[index].next;
    q->slots[index].state = VQ_FREE;
    q->slots[index].cookie = 0;
    q->slots[index].writable = 0;
    q->free++;
    if (next == VQ_NONE) {
      q->slots[index].next = (u16) q->head;
      break;
    }
    index = next;
  } while (1);
  q->head = head;
}
int
vqcancel(Vq *q, u32 head)
{
  int error = ready(q);
  if (error)
    return error;
  if (head >= VQ_SIZE || q->slots[head].state != VQ_RESERVED)
    return -ERR_STATE;
  release(q, head);
  return 0;
}
int
vqcomplete(Vq *q, u64 generation, const Vused *used, u32 count, Vdone *out)
{
  u64 seen[2], bit;
  u32 i, head;
  int error = ready(q);
  if (error)
    return error;
  if (generation != q->generation)
    return -ERR_STALE;
  if (count > VQ_BATCH || (count && (!used || !out)))
    return -ERR_INVALID;
  seen[0] = seen[1] = 0;
  for (i = 0; i < count; i++) {
    head = used[i].id;
    if (head >= VQ_SIZE || q->slots[head].state != VQ_PUBLISHED ||
        used[i].length > q->slots[head].writable)
      goto broken;
    bit = (u64) 1 << (head & 63);
    if (seen[head >> 6] & bit)
      goto broken;
    seen[head >> 6] |= bit;
  }
  for (i = 0; i < count; i++) {
    head = used[i].id;
    out[i].cookie = q->slots[head].cookie;
    out[i].length = used[i].length;
    out[i].reserved = 0;
    release(q, head);
  }
  return 0;
broken:
  q->broken = 1;
  return -ERR_PEER;
}
