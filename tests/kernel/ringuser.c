/* CPU-backed device fixture only: these pages are not native DMA grants. */
#include "ringuser.h"
#include "private/virtio/ring.h"
#include "private/virtio/block.h"
#include "private/serial.h"
#include <unistd.h>
static u8         pages[3][PAGE_SIZE] ALIGNED(PAGE_SIZE);
static Serialring serialring;
#define CHECK(c)                                                                                   \
  do {                                                                                             \
    if (!(c))                                                                                      \
      _exit(229);                                                                                  \
  } while (0)
static void
serialcheck(void)
{
  u8    source[SERIAL_BATCH], output[SERIAL_BATCH];
  u32   i, j;
  isize amount;
  CHECK(!serialinit(&serialring, 1));
  for (i = 0; i < SERIAL_BATCH; i++)
    source[i] = (u8) i;
  CHECK(serialwrite(&serialring, 2, source, 1) == -ERR_STALE);
  CHECK(serialwrite(&serialring, 1, serialring.data, 1) == -ERR_INVALID);
  for (i = 0; i < SERIAL_CAPACITY / SERIAL_BATCH; i++)
    CHECK(serialwrite(&serialring, 1, source, SERIAL_BATCH) == SERIAL_BATCH);
  CHECK(!serialwrite(&serialring, 1, source, 1));
  CHECK(serialread(&serialring, 1, output, 17) == 17);
  for (i = 0; i < 17; i++)
    CHECK(output[i] == i);
  CHECK(serialwrite(&serialring, 1, source, SERIAL_BATCH) == 17);
  for (i = 0; i < SERIAL_CAPACITY; i += SERIAL_BATCH) {
    amount = serialread(&serialring, 1, output, SERIAL_BATCH);
    CHECK(amount == SERIAL_BATCH);
    for (j = 0; j < SERIAL_BATCH; j++)
      CHECK(output[j] == (i + j < SERIAL_CAPACITY - 17 ? (i + j + 17) % SERIAL_BATCH
                                                       : i + j - (SERIAL_CAPACITY - 17)));
  }
  CHECK(serialring.producer == 8209 && serialring.consumer == 8209);
  serialring.consumer++;
  CHECK(serialwrite(&serialring, 1, source, 1) == -ERR_PEER);
  CHECK(!serialinit(&serialring, 3));
  serialring.producer = serialring.consumer = UINT64_MAX - 1;
  CHECK(serialwrite(&serialring, 3, source, 2) == -ERR_OVERFLOW);
  CHECK(serialwrite(&serialring, 3, source, 1) == 1);
  __atomic_store_n(&serialring.flags, SERIAL_EOF, __ATOMIC_RELEASE);
  CHECK(serialwrite(&serialring, 3, source, 1) == -ERR_STATE);
  CHECK(serialread(&serialring, 3, output, SERIAL_BATCH) == 1 && !output[0]);
  CHECK(!serialread(&serialring, 3, output, SERIAL_BATCH));
  CHECK(serialring.producer == UINT64_MAX && serialring.consumer == UINT64_MAX);
}
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
  Vbuf     buffers[3];
  Vblkhead request;
  Vdone    done[VQ_BATCH];
  u64      counts[3];
  u32      published, count, more, head;
  serialcheck();
  counts[0] = counts[1] = counts[2] = 0;
  ops.context = counts;
  ops.sync = ringsync;
  ops.fence = ringfence;
  ops.notify = ringnotify;
  CHECK(!vrinit(&r, 11, pages[0], pages[1], pages[2], &ops));
  CHECK(counts[0] == 3 && counts[1] == 1 && !counts[2]);
  CHECK(vblkprepare(VBLK_READ, 8, 16, 0x10000, 0x20000, 4096, 0x30000, &request, buffers) == 3 &&
        request.type == VBLK_READ && !request.reserved && request.sector == 8 &&
        !vrsubmit(&r, buffers, 3, 77, &published) && published && counts[2] == 1);
  head = r.avail->ring[0];
  CHECK(r.avail->index == 1 && r.desc[head].flags == 1 && r.desc[r.desc[head].next].flags == 3);
  r.used->ring[0].id = head;
  r.used->ring[0].length = 4097;
  r.used->index = 1;
  CHECK(!vrpoll(&r, 11, done, &count, &more) && count == 1 && !more && done[0].cookie == 77 &&
        !vblkresult(VBLK_READ, 4096, done[0].length, VBLK_OK) && r.queue.free == VQ_SIZE &&
        vblkresult(VBLK_READ, 4096, 1, VBLK_OK) == -ERR_PEER &&
        vblkresult(VBLK_READ, 4096, 1, VBLK_IOERR) == VBLK_IOERR);
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
