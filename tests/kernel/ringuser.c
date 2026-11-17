/* CPU-backed device fixture only: these pages are not native DMA grants. */
#include "ringuser.h"
#include "private/virtio/ring.h"
#include <unistd.h>
static u8 pages[3][PAGE_SIZE] ALIGNED(PAGE_SIZE);
#define CHECK(c)                                                                                   \
  do {                                                                                             \
    if (!(c))                                                                                      \
      _exit(229);                                                                                  \
  } while (0)
static int
ringsync(void *context, u32 region, u32 phase, u64 offset, u64 length)
{
  u64 *counts = context;
  if (region > VR_USED || phase > VR_AFTER || !length || length > PAGE_SIZE ||
      offset > PAGE_SIZE - length || (phase == VR_AFTER && region != VR_USED))
    return -ERR_INVALID;
  counts[0]++;
  __sync_synchronize();
  return 0;
}
static void
ringfence(void *context, u32 phase)
{
  u64 *counts = context;
  CHECK(phase <= VR_ACQUIRE);
  counts[1]++;
  __sync_synchronize();
}
static int
ringnotify(void *context)
{
  u64 *counts = context;
  counts[2]++;
  __sync_synchronize();
  return 0;
}
u32
ringusercheck(void)
{
  Vring    r;
  Vringops ops;
  Vbuf     buffers[2];
  Vdone    done[VQ_BATCH];
  u64      counts[3];
  u32      published, count, more, head;
  counts[0] = counts[1] = counts[2] = 0;
  ops.context = counts;
  ops.sync = ringsync;
  ops.fence = ringfence;
  ops.notify = ringnotify;
  CHECK(!vrinit(&r, 11, pages[0], pages[1], pages[2], &ops));
  CHECK(counts[0] == 3 && counts[1] == 1 && !counts[2]);
  buffers[0].address = 0x10000;
  buffers[0].length = 16;
  buffers[0].writable = 0;
  buffers[1].address = 0x20000;
  buffers[1].length = 32;
  buffers[1].writable = 1;
  CHECK(!vrsubmit(&r, buffers, 2, 77, &published) && published && counts[2] == 1);
  head = r.avail->ring[0];
  CHECK(r.avail->index == 1 && r.desc[head].flags == 1 && r.desc[r.desc[head].next].flags == 2);
  r.used->ring[0].id = head;
  r.used->ring[0].length = 32;
  r.used->index = 1;
  CHECK(!vrpoll(&r, 11, done, &count, &more) && count == 1 && !more && done[0].cookie == 77 &&
        done[0].length == 32 && r.queue.free == VQ_SIZE);
  r.nextavail = r.nextused = 65535;
  r.avail->index = r.used->index = 65535;
  CHECK(!vrsubmit(&r, &buffers[1], 1, 88, &published) && published && !r.nextavail);
  r.used->ring[127].id = r.avail->ring[127];
  r.used->ring[127].length = 1;
  r.used->index = 0;
  CHECK(!vrpoll(&r, 11, done, &count, &more) && count == 1 && !more && done[0].cookie == 88 &&
        !r.nextused && r.queue.free == VQ_SIZE);
  CHECK(!vrsubmit(&r, &buffers[1], 1, 99, &published) && published);
  r.used->ring[0].id = VQ_SIZE;
  r.used->ring[0].length = 1;
  r.used->index = 1;
  CHECK(vrpoll(&r, 11, done, &count, &more) == -ERR_PEER && !count && !more && r.queue.broken &&
        r.queue.free == VQ_SIZE - 1 && counts[2] == 3);
  return 3;
}
