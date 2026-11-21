#include <unistd.h>
#include <sys/channel.h>
#include <abi/time.h>
#include <abi/syscall.h>
#include <abi/irq.h>
#include "private/syscall.h"
#include "private/start.h"
#include "../../sbin/blk/io.h"
#include "../../sbin/fs/journal.h"
#include "virtiouser.h"
volatile u64             vdev_trace[VTEST_TRACE];
typedef struct Testdma   Testdma;
typedef struct Resetsync Resetsync;
struct Testdma
{
  u64                 va;
  const volatile u64 *addresses;
};
struct Resetsync
{
  Vdev *device;
  u32   after;
};
#define REQUIRE(c, n)                                                                              \
  do {                                                                                             \
    if (!(c)) {                                                                                    \
      vdev_trace[31] = (n);                                                                        \
      _exit(n);                                                                                    \
    }                                                                                              \
  } while (0)
static u64
now(void)
{
  const u64 args[7] = {CLOCK_MONOTONIC, 0, 0, 0, 0, 0, 0};
  Sysret    result;
  REQUIRE(!__sys_enter(SYS_CLOCK_GETTIME, args, &result) && result.values[1] < 1000000000 &&
              result.values[0] <= (~(u64) 0 - 999999999) / 1000000000,
          161);
  return result.values[0] * 1000000000 + result.values[1];
}
static void
reset(Vdev *device)
{
  int error = vresetbegin(device, now());
  /* Only this hardware fixture polls synchronously; the library is one step
     per call and production drivers must integrate their event loop. */
  while (error == -ERR_WOULD_BLOCK)
    error = vresetpoll(device, now());
  REQUIRE(!error && device->state == VDEV_RESET && !device->status, 162);
  vdev_trace[16]++;
}
static int
coherentsync(void *context, u32 region, u32 phase, u64 offset, u64 length)
{
  UNUSED(region);
  UNUSED(offset);
  UNUSED(length);
  /* This fixture only runs on QEMU virt's coherent guest RAM. */
  virtiofence(context, phase);
  return 0;
}
static int
notify(void *context)
{
  return vnotify(context, 0);
}
static int
blksync(void *context, u32 phase, const Vbuf *buffer)
{
  UNUSED(buffer);
  virtiofence(context, phase == VR_BEFORE ? VR_RELEASE : VR_ACQUIRE);
  return 0;
}
static int
resetsync(void *context, u32 phase, const Vbuf *buffer)
{
  Resetsync *sync = context;
  if (phase == VR_AFTER) {
    REQUIRE(sync->device->state == VDEV_RESET && !sync->device->status, 228);
    if (++sync->after == 2)
      return -ERR_BUSY;
  }
  return blksync(sync->device, phase, buffer);
}
static void
testbuffer(const Testdma *dma, u32 page, Blkdma *buffer)
{
  buffer->header = (void *) (dma->va + page * PAGE_SIZE);
  buffer->data = (void *) (dma->va + (page + 1) * PAGE_SIZE);
  buffer->status = (void *) (dma->va + (page + 2) * PAGE_SIZE);
  buffer->headaddr = dma->addresses[page];
  buffer->dataaddr = dma->addresses[page + 1];
  buffer->statusaddr = dma->addresses[page + 2];
  buffer->capacity = PAGE_SIZE;
}
static void
transfer(Vdev *device, Vring *ring, u64 epoch, Handle binding, u64 irqevent,
         const Vblkconfig *config, const Testdma *dma, u32 type, u64 sector, u64 cookie, u8 seed,
         Vdone *prior, const u8 *image)
{
  static u8 staged[PAGE_SIZE];
  Vblkreq   request = {0};
  Vdone     done[VQ_BATCH];
  Ipcmsg    event;
  Blkdma    buffer;
  const u8 *source = image;
  u8       *destination = type == VBLK_READ ? staged : NULL;
  u32       bytes = type == VBLK_FLUSH ? 0 : config->blocksize;
  u32       count, more, i, causes;
  u64       generation, started;
  testbuffer(dma, 3, &buffer);
  REQUIRE(bytes <= sizeof(staged), 204);
  if (type == VBLK_WRITE && !source) {
    for (i = 0; i < bytes; i++)
      staged[i] = (u8) (seed + i * 37);
    source = staged;
  }
  if (destination)
    for (i = 0; i < bytes; i++)
      destination[i] = 0xa5;
  started = now();
  REQUIRE(started <= UINT64_MAX - VDEV_RESET_NS &&
              !__sys_alarmset(started + VDEV_RESET_NS, &generation) && !__sys_irqack(binding),
          179);
  REQUIRE(!blkiosubmit(&request, ring, config, &buffer, type, sector, bytes, cookie, source,
                       blksync, device),
          174);
  REQUIRE(!ipc_recv(irqevent | EVENT_TIMER, RECV_EVENTS_ONLY, &event) && event.kind == IPC_EVENT &&
              event.words[0] == irqevent,
          176);
  REQUIRE(!__sys_alarmcancel(&generation), 180);
  /* The binding masks the line on delivery. Clear the device cause before
     consuming used entries; the next request rearms only after this drains. */
  if (prior && prior->generation)
    REQUIRE(prior->cookie == cookie &&
                blkiofinish(&request, prior, &buffer, destination, bytes) == -ERR_STALE &&
                request.active && request.held == 7,
            190);
  REQUIRE(!blkirqpoll(device, ring, epoch, done, &count, &more, &causes) &&
              causes == VDEV_IRQ_USED && !virtioread(device->ops.context, 0x60, &causes) && !causes,
          181);
  REQUIRE(count == 1 && !more && done[0].cookie == cookie && ring->queue.free == VQ_SIZE, 207);
  if (destination) {
    REQUIRE(blkiofinish(&request, &done[0], &buffer, destination, bytes - 1) == -ERR_INVALID &&
                request.active,
            205);
    for (i = 0; i < bytes; i++)
      REQUIRE(destination[i] == 0xa5, 206);
  }
  REQUIRE(blkiofinish(&request, &done[0], &buffer, destination, destination ? bytes : 0) ==
                  VBLK_OK &&
              !request.held,
          177);
  if (prior)
    *prior = done[0];
  vdev_trace[38]++;
  if (type == VBLK_READ) {
    for (i = 0; i < bytes; i++)
      REQUIRE(destination[i] == (u8) (seed + i * 37), 178);
    vdev_trace[22]++;
  } else if (type == VBLK_WRITE)
    vdev_trace[2]++;
  else
    vdev_trace[3]++;
}
static void
testredo(Vdev *device, Vring *ring, Handle binding, u64 irqevent, const Vblkconfig *config,
         const Testdma *dma)
{
  static Journal journal;
  static u8      images[3][JOURNAL_BYTES];
  Journalblock   metadata[2];
  Journalio      io;
  Fsgeometry     geometry;
  u32            i, j, count = 0;
  u64            stride = JOURNAL_BYTES / VBLK_SECTOR;
  u64            blocks = config->sectors / stride;
  REQUIRE(config->blocksize == JOURNAL_BYTES, 193);
  geometry.bytes = config->sectors * VBLK_SECTOR;
  geometry.logical = config->blocksize;
  geometry.physical = config->physical;
  geometry.alignment = config->alignment;
  geometry.readonly = config->readonly;
  geometry.flush = config->flush;
  geometry.faultbytes = geometry.faultoffset = 0;
  REQUIRE(!fsgeometry(&geometry, blocks, 0) && fsgeometry(&geometry, blocks, 1) == -ERR_NOTSUP &&
              fsgeometry(&geometry, blocks + 1, 0) == -ERR_STATE,
          199);
  geometry.faultbytes = 2 * FS_BLOCK_BYTES;
  REQUIRE(fsgeometry(&geometry, blocks, 1) == -ERR_NOTSUP, 200);
  /* This harness provisions an aligned 4 KiB boundary. It does not
     derive the failure model from the topology advertised by virtio. */
  geometry.faultbytes = FS_BLOCK_BYTES;
  geometry.faultoffset = VBLK_SECTOR;
  REQUIRE(fsgeometry(&geometry, blocks, 1) == -ERR_NOTSUP, 201);
  geometry.faultoffset = 0;
  geometry.flush = 0;
  REQUIRE(!fsgeometry(&geometry, blocks, 0) && fsgeometry(&geometry, blocks, 1) == -ERR_NOTSUP,
          203);
  geometry.flush = config->flush;
  REQUIRE(!fsgeometry(&geometry, blocks, 1), 202);
  for (i = 0; i < ARRAY_SIZE(images); i++)
    for (j = 0; j < JOURNAL_BYTES; j++)
      images[i][j] = (u8) (0x92 + i * 31 + j * 37);
  metadata[0].block = 65;
  metadata[0].bytes = images[0];
  metadata[1].block = 66;
  metadata[1].bytes = images[1];
  /* Synthetic snapshots exercise transport and ordering before a disk
     codec exists. The independent host census checks the written bytes. */
  REQUIRE(!journalredo(&journal, blocks, 47, metadata, 2, images[2]), 194);
  while (journal.phase != JOURNAL_DONE) {
    REQUIRE(count < 5 && !journalnext(&journal, &io) && io.txid == 47, 195);
    transfer(device, ring, 2, binding, irqevent, config, dma,
             io.kind == JOURNAL_WRITE ? VBLK_WRITE : VBLK_FLUSH, io.block * stride, 2 + count, 0,
             NULL, io.bytes);
    REQUIRE(!journalcomplete(&journal, &io, 1), 196);
    count++;
  }
  REQUIRE(count == 5 && !journalrelease(&journal) && journal.last == 47, 197);
}
static void
startqueue(Vdev *device, Vring *ring, u64 epoch, const Testdma *dma)
{
  Vringops ops;
  u8      *desc = (void *) dma->va;
  ops.context = device;
  ops.sync = coherentsync;
  ops.fence = virtiofence;
  ops.notify = notify;
  REQUIRE(!vrinit(ring, epoch, desc, desc + PAGE_SIZE, desc + 2 * PAGE_SIZE, &ops), 170);
  REQUIRE(!vqueue(device, 0, dma->addresses[0], dma->addresses[1], dma->addresses[2]), 171);
  REQUIRE(!vstart(device), 172);
}
static void
resetpending(Vdev *device, Vring *ring, Handle binding, u64 irqevent, const Vblkconfig *config,
             const Testdma *dma, Blkqueue *queue, Vblkreq *request, Blkdma *buffer)
{
  Resetsync sync = {device, 0};
  Ipcmsg    event;
  u64       generation, started = now();
  u32       causes;
  int       error;

  testbuffer(dma, 3, buffer);
  REQUIRE(!blkqinit(queue, device, ring, request, buffer, 1), 224);
  REQUIRE(started <= UINT64_MAX - VDEV_RESET_NS &&
              !__sys_alarmset(started + VDEV_RESET_NS, &generation) && !__sys_irqack(binding),
          185);
  REQUIRE(!blkqsubmit(queue, 0, config, VBLK_READ, config->blocksize / VBLK_SECTOR,
                      config->blocksize, NULL, resetsync, &sync),
          186);
  REQUIRE(!ipc_recv(irqevent | EVENT_TIMER, RECV_EVENTS_ONLY, &event) && event.kind == IPC_EVENT &&
              event.words[0] == irqevent && !__sys_alarmcancel(&generation),
          187);
  /* Leave the device cause and used entry unconsumed. The IRQ alone does
     not return ownership of the request buffers. */
  REQUIRE(!virtioread(device->ops.context, 0x60, &causes) && causes == VDEV_IRQ_USED &&
              request->active && request->held == 7 && ring->queue.free == VQ_SIZE - 3,
          188);
  do {
    error = blkqrecover(queue, now());
  } while (error == -ERR_WOULD_BLOCK);
  REQUIRE(error == -ERR_BUSY && sync.after == 2 && request->held == 6 &&
              blkqinit(queue, device, ring, request, buffer, 1) == -ERR_STATE &&
              blkqsubmit(queue, 0, config, VBLK_READ, 0, config->blocksize, NULL, blksync,
                         device) == -ERR_STATE,
          229);
  REQUIRE(!virtioread(device->ops.context, 0x60, &causes) && !causes &&
              !blkqrecover(queue, now()) && sync.after == 4 && !request->active && !request->held &&
              blkqinit(queue, device, ring, request, buffer, 1) == -ERR_STALE,
          189);
  vdev_trace[16]++;
  vdev_trace[39]++;
}
static void
testresize(Vdev *device, Vring *ring, Handle binding, u64 irqevent, const Vblkconfig *old)
{
  Vblkconfig refreshed;
  Ipcmsg     event;
  Vdone      done[VQ_BATCH];
  u32        count, more, causes;
  u64        alarm, started = now();
  REQUIRE(started <= UINT64_MAX - VDEV_RESET_NS &&
              !__sys_alarmset(started + VDEV_RESET_NS, &alarm) && !__sys_irqack(binding),
          208);
  vdev_trace[VTEST_RESIZE] = 1;
  for (;;) {
    REQUIRE(!ipc_recv(irqevent | EVENT_TIMER, RECV_EVENTS_ONLY, &event) &&
                event.kind == IPC_EVENT && event.words[0] == irqevent,
            209);
    REQUIRE(blkirqpoll(device, ring, 1, done, &count, &more, &causes) == -ERR_STALE && !causes &&
                !count && !more,
            210);
    REQUIRE(!blkirqpoll(device, ring, 2, done, &count, &more, &causes) && !count && !more, 211);
    if (causes & VDEV_IRQ_CONFIG)
      break;
    REQUIRE(!__sys_irqack(binding), 226);
  }
  REQUIRE(!__sys_alarmcancel(&alarm), 227);
  REQUIRE(!vblkconfig(device, &refreshed) && refreshed.sectors == 2 * old->sectors &&
              refreshed.blocksize == old->blocksize && refreshed.physical == old->physical &&
              refreshed.alignment == old->alignment && refreshed.flush == old->flush &&
              refreshed.readonly == old->readonly,
          212);
  REQUIRE(!blkirqpoll(device, ring, 2, done, &count, &more, &causes) && !count && !more && !causes,
          213);
  vdev_trace[VTEST_CAPACITY] = refreshed.sectors;
}
static void
testparallel(Vdev *device, Vring *ring, Handle binding, u64 irqevent, const Vblkconfig *config,
             const Testdma *dma)
{
  static u8 staged[PAGE_SIZE];
  Vblkreq   requests[2];
  Blkdma    buffers[2];
  Blkqueue  queue = {0};
  Ipcmsg    event;
  Vdone     done[VQ_BATCH], stale;
  u64       stride = config->blocksize / VBLK_SECTOR;
  u64       alarm, started = now();
  u32       count, causes, more, i, j, seen = 0;
  int       slot;
  testbuffer(dma, 3, &buffers[0]);
  testbuffer(dma, 6, &buffers[1]);
  REQUIRE(!blkqinit(&queue, device, ring, requests, buffers, ARRAY_SIZE(requests)), 214);
  REQUIRE(started <= UINT64_MAX - VDEV_RESET_NS &&
              !__sys_alarmset(started + VDEV_RESET_NS, &alarm) && !__sys_irqack(binding),
          215);
  REQUIRE(
      !blkqsubmit(&queue, 0, config, VBLK_READ, stride, config->blocksize, NULL, blksync, device) &&
          !blkqsubmit(&queue, 1, config, VBLK_READ, 2 * stride, config->blocksize, NULL, blksync,
                      device) &&
          blkqsubmit(&queue, 0, config, VBLK_READ, 0, config->blocksize, NULL, blksync, device) ==
              -ERR_BUSY,
      216);
  while (seen != 3) {
    REQUIRE(!ipc_recv(irqevent | EVENT_TIMER, RECV_EVENTS_ONLY, &event) &&
                event.kind == IPC_EVENT && event.words[0] == irqevent,
            217);
    REQUIRE(!blkirqpoll(device, ring, 2, done, &count, &more, &causes) &&
                !(causes & VDEV_IRQ_CONFIG) && !more,
            218);
    for (i = 0; i < count; i++) {
      slot = blkqmatch(&queue, &done[i]);
      REQUIRE(slot >= 0 && slot < 2 && !(seen & (1U << slot)), 219);
      stale = done[i];
      stale.generation--;
      REQUIRE(blkqfinish(&queue, &stale, staged, sizeof(staged)) == -ERR_STALE, 225);
      stale = done[i];
      stale.cookie += VQ_SIZE;
      REQUIRE(blkqmatch(&queue, &stale) == -ERR_STALE &&
                  !blkqfinish(&queue, &done[i], staged, sizeof(staged)) &&
                  blkqmatch(&queue, &done[i]) == -ERR_STALE,
              220);
      for (j = 0; j < config->blocksize; j++)
        REQUIRE(staged[j] == (u8) ((slot ? 0x37 : 0x51) + j * 37), 221);
      seen |= 1U << slot;
    }
    if (seen != 3)
      REQUIRE(!__sys_irqack(binding), 222);
  }
  REQUIRE(!__sys_alarmcancel(&alarm) && ring->queue.free == VQ_SIZE, 223);
  vdev_trace[VTEST_PARALLEL] = 2;
}
static void
testblock(Vdev *device, const Vblkconfig *config, u32 member)
{
  Vring      ring;
  Vdone      stale[VQ_BATCH];
  Vdone      prior = {0};
  Blkqueue   queue = {0};
  Vblkreq    request;
  Blkdma     buffer;
  Vblkconfig fresh;
  Testdma    dma;
  u32        count, more;
  Handle     binding = bootresource(sysboot(), VTEST_IRQ_TAG + member);
  u64        irqevent = UINT64_C(1) << (IRQ_BIT_MIN + member);
  u64        stride = config->blocksize / VBLK_SECTOR;

  REQUIRE(binding > 0, 182);
  dma.va = VTEST_DMA_BASE + member * VTEST_PAGES * PAGE_SIZE;
  dma.addresses = &vdev_trace[VTEST_DMA_TRACE + member * VTEST_PAGES];
  startqueue(device, &ring, 1, &dma);
  transfer(device, &ring, 1, binding, irqevent, config, &dma, VBLK_READ, stride, 1,
           config->readonly ? 0xa7 : 0x51, &prior, NULL);
  if (!config->readonly) {
    transfer(device, &ring, 1, binding, irqevent, config, &dma, VBLK_WRITE, 2 * stride, 2, 0x37,
             NULL, NULL);
    transfer(device, &ring, 1, binding, irqevent, config, &dma, VBLK_FLUSH, 0, 3, 0, NULL, NULL);
    transfer(device, &ring, 1, binding, irqevent, config, &dma, VBLK_READ, 2 * stride, 4, 0x37,
             NULL, NULL);
  }
  resetpending(device, &ring, binding, irqevent, config, &dma, &queue, &request, &buffer);
  REQUIRE(!vblkfeatures(device) && !vblkconfig(device, &fresh) &&
              fresh.sectors == config->sectors && fresh.readonly == config->readonly &&
              fresh.flush == config->flush && fresh.blocksize == config->blocksize &&
              fresh.physical == config->physical && fresh.alignment == config->alignment,
          183);
  startqueue(device, &ring, 2, &dma);
  REQUIRE(!blkqinit(&queue, device, &ring, &request, &buffer, 1) &&
              blkqmatch(&queue, &prior) == -ERR_STALE,
          230);
  REQUIRE(vrpoll(&ring, 1, stale, &count, &more) == -ERR_STALE && !count && !more, 184);
  REQUIRE(prior.generation == 1, 191);
  transfer(device, &ring, 2, binding, irqevent, &fresh, &dma, VBLK_READ,
           config->readonly ? stride : 2 * stride, 1, config->readonly ? 0xa7 : 0x37, &prior, NULL);
  REQUIRE(prior.generation == 2, 192);
  if (!config->readonly) {
    testredo(device, &ring, binding, irqevent, &fresh, &dma);
    testparallel(device, &ring, binding, irqevent, &fresh, &dma);
    testresize(device, &ring, binding, irqevent, &fresh);
  }
}
int
main(int argc, char **argv)
{
  Handle  memory = bootresource(sysboot(), 257), space = bootresource(sysboot(), BOOT_SELF_SPACE);
  Maparg  mapping;
  Vdev    device;
  Vdevops ops;
  Vblkconfig config;
  u64        base;
  u32        i, status;
  UNUSED(argc);
  UNUSED(argv);
  REQUIRE(memory > 0 && vdev_trace[1] && vdev_trace[1] <= 8, 151);
  mapping.mem = memory;
  mapping.offset = 0;
  mapping.address = 0x2000000;
  mapping.length = PAGE_SIZE;
  mapping.prot = MAP_READ | MAP_WRITE;
  mapping.reserved = 0;
  REQUIRE(!__sys_map(space, &mapping) && !__sys_free(memory), 152);
  ops.context = &base;
  ops.read = virtioread;
  ops.write = virtiowrite;
  ops.readbyte = virtioreadbyte;
  for (i = 0; i < vdev_trace[1]; i++) {
    REQUIRE(vdev_trace[4 + i] <= PAGE_SIZE - 0x100 && vdev_trace[23 + i] >= 0x100, 153);
    base = mapping.address + vdev_trace[4 + i];
    REQUIRE(!vprobe(&device, &ops), 154);
    vdev_trace[12]++;
    if (!device.device) {
      REQUIRE(device.state == VDEV_EMPTY && !device.vendor, 155);
      vdev_trace[13]++;
      continue;
    }
    REQUIRE((device.device == 4 || device.device == VBLK_DEVICE) && device.state == VDEV_FOUND,
            156);
    vdev_trace[14]++;
    reset(&device);
    if (device.device == VBLK_DEVICE) {
      REQUIRE(vdev_trace[4 + i] <= PAGE_SIZE - 0x11a && vdev_trace[23 + i] >= 0x11a, 163);
      REQUIRE(!vblkfeatures(&device) && !vblkconfig(&device, &config), 164);
      REQUIRE((device.features & VBLK_F_TOPOLOGY) && config.physical == PAGE_SIZE &&
                  !config.alignment,
              198);
      if (config.readonly) {
        REQUIRE((device.features & VBLK_F_RO) && config.sectors == 128 && config.blocksize == 512,
                165);
        vdev_trace[19]++;
        vdev_trace[21] = config.sectors;
      } else {
        REQUIRE(!(device.features & VBLK_F_RO) && config.flush && config.sectors == 1024 &&
                    config.blocksize == PAGE_SIZE,
                166);
        vdev_trace[18]++;
        vdev_trace[20] = config.sectors;
      }
    } else {
      REQUIRE(!vfeatures(&device, 0, 0) && device.features == VDEV_VERSION1, 157);
      vdev_trace[17]++;
    }
    REQUIRE(device.state == VDEV_FEATURES && (device.features & VDEV_VERSION1) &&
                (device.offered & device.features) == device.features,
            167);
    REQUIRE(!virtioread(&base, 0x70, &status) && status == 11, 158);
    vdev_trace[15]++;
    if (device.device == VBLK_DEVICE)
      testblock(&device, &config, i);
    reset(&device);
    REQUIRE(!virtioread(&base, 0x70, &status) && !status, 159);
  }
  REQUIRE(!__sys_unmap(space, mapping.address), 160);
  vdev_trace[0] = 99;
  return 42;
}
