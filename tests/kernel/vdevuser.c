#include <unistd.h>
#include <sys/channel.h>
#include <abi/time.h>
#include <abi/syscall.h>
#include <abi/irq.h>
#include "private/syscall.h"
#include "private/start.h"
#include "../../sbin/blk/io.h"
#include "../../sbin/fs/cache.h"
#include "../../sbin/fs/load.h"
#include "../../sbin/fs/sync.h"
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
  u32   after, failbefore;
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
  if (phase == VR_BEFORE && sync->failbefore)
    return -ERR_FAULT;
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
static int
redometa(void *context, const Journalblock *images, u32 count, u32 index)
{
  (void) context;
  return count == 2 && (images[index].block == 65 || images[index].block == 66);
}

static void
testredo(Vdev *device, Vring *ring, Handle binding, u64 irqevent, const Vblkconfig *config,
         const Testdma *dma)
{
  static Journal journal, idle;
  static u8      images[3][JOURNAL_BYTES];
  Journalblock   metadata[2];
  Journalio      io;
  Fsgeometry     geometry;
  u32            i, j, count = 0;
  u64            stride = JOURNAL_BYTES / VBLK_SECTOR;
  u64            blocks = config->sectors / stride;
  Fslayout       layout = {blocks, 1, 64, 65, 66, 67, 99, 1, 4096, 1024, 1};
  Journalmark    mark = {47, 0, 0, 0};
  Journalhead    head = {47, {65, 66}, 1, 2, 11, 12};
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
  REQUIRE(!fslayoutcheck(&layout, &geometry, 1), 242);
  layout.blockmap = layout.inodemap;
  REQUIRE(fslayoutcheck(&layout, &geometry, 0) == -ERR_STATE, 243);
  layout.blockmap = 66;
  layout.root = 0;
  REQUIRE(fslayoutcheck(&layout, &geometry, 0) == -ERR_STATE, 244);
  layout.root = 1;
  layout.blocks++;
  REQUIRE(fslayoutcheck(&layout, &geometry, 0) == -ERR_STATE, 245);
  REQUIRE(journalrecover(&journal, &layout, &geometry, &mark, NULL, NULL, 0, 0, NULL, NULL, NULL) ==
                  -ERR_STATE &&
              !journal.last,
          249);
  layout.blocks = blocks;
  for (i = 0; i < ARRAY_SIZE(images); i++)
    for (j = 0; j < JOURNAL_BYTES; j++)
      images[i][j] = (u8) (0x92 + i * 31 + j * 37);
  metadata[0].block = 65;
  metadata[0].bytes = images[0];
  metadata[1].block = 66;
  metadata[1].bytes = images[1];
  /* Synthetic snapshots exercise transport and ordering before a disk
     codec exists. The independent host census checks the written bytes. */
  geometry.flush = 0;
  REQUIRE(journalrecover(&journal, &layout, &geometry, &mark, NULL, NULL, 0, 0, NULL, NULL, NULL) ==
                  -ERR_NOTSUP &&
              !journal.last,
          250);
  geometry.flush = config->flush;
  REQUIRE(!journalrecover(&idle, &layout, &geometry, &mark, NULL, NULL, 0, 0, NULL, NULL, NULL) &&
              idle.last == 47 && journalnext(&idle, &io) == -ERR_STATE &&
              journalrecover(&idle, &layout, &geometry, &mark, NULL, NULL, 0, 0, NULL, NULL,
                             NULL) == -ERR_STATE,
          246);
  mark = (Journalmark){47, 1, 11, 12};
  REQUIRE(journalrecover(&journal, &layout, &geometry, &mark, &head, metadata, 2, 13, images[2],
                         redometa, NULL) == -ERR_STATE &&
              journalnext(&journal, &io) == -ERR_STATE,
          247);
  head.targets[1] = metadata[1].block = 99;
  REQUIRE(journalrecover(&journal, &layout, &geometry, &mark, &head, metadata, 2, 12, images[2],
                         redometa, NULL) == -ERR_STATE &&
              journalnext(&journal, &io) == -ERR_STATE,
          248);
  head.targets[1] = metadata[1].block = 66;
  geometry.faultbytes = 0;
  REQUIRE(journalrecover(&journal, &layout, &geometry, &mark, &head, metadata, 2, 12, images[2],
                         redometa, NULL) == -ERR_NOTSUP &&
              !journal.last && journalnext(&journal, &io) == -ERR_STATE,
          251);
  geometry.faultbytes = FS_BLOCK_BYTES;
  REQUIRE(!journalrecover(&journal, &layout, &geometry, &mark, &head, metadata, 2, 12, images[2],
                          redometa, NULL),
          194);
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
static isize
syncsubmit(void *context, Handle request, const Journalio *io)
{
  Journalio *pending = context;
  (void) request;
  *pending = *io;
  return 0;
}

static void
testwriteback(Vdev *device, Vring *ring, Handle binding, u64 irqevent, const Vblkconfig *config,
              const Testdma *dma)
{
  static Txn        txn;
  static Cacheblock entry;
  static u8         image[CACHE_BYTES], snapshot[CACHE_BYTES];
  Cache             cache;
  Cachekey          key = {0};
  Cacheio           fill;
  Journalio         io;
  u8               *destination;
  u8                later = 0x73;
  const Loadmap     map = {{2, 1, 1, CACHE_BYTES}, {FS_DATA_START}, 0};
  const Handle      request = ((u64) HANDLE_REQUEST << 59) | (1U << 16);
  Syncwork          work = {&txn, snapshot, 1, 0};
  u32               i;
  u64               stride = CACHE_BYTES / VBLK_SECTOR;
  REQUIRE(!cacheinit(&cache, &entry, 1, config->sectors / stride) && !txninit(&txn, &cache, 47) &&
              !cacheget(&cache, FS_DATA_START, CACHE_DATA, &key) &&
              !cachereadbegin(&cache, &key, &fill, &destination) &&
              !cachecomplete(&cache, &fill, 1),
          260);
  for (i = 0; i < CACHE_BYTES; i++)
    image[i] = (u8) (0xb4 + i * 37);
  REQUIRE(!cacheedit(&cache, &key, 0, image, CACHE_BYTES) &&
              !filesyncstep(&work, request, RQ_CHECK_LIVE, &map, NULL, syncsubmit, &io) &&
              work.request == request &&
              !filesyncstep(&work, request + 1, RQ_CHECK_LIVE, &map, NULL, syncsubmit, &io) &&
              work.request == request && io.serial == 1 && io.kind == JOURNAL_WRITE &&
              io.block == FS_DATA_START && !cacheedit(&cache, &key, 0, &later, 1),
          261);
  transfer(device, ring, 2, binding, irqevent, config, dma, VBLK_WRITE, io.block * stride, 40, 0,
           NULL, io.bytes);
  REQUIRE(filesynccomplete(&work, request + 1, &io, 1) == -ERR_STALE &&
              !filesynccomplete(&work, request, &io, 1) && entry.dirty && !txn.journal.durable &&
              !filesyncstep(&work, request, RQ_CHECK_LIVE, &map, NULL, syncsubmit, &io) &&
              io.kind == JOURNAL_FLUSH && io.serial == 2,
          262);
  transfer(device, ring, 2, binding, irqevent, config, dma, VBLK_FLUSH, 0, 41, 0, NULL, NULL);
  REQUIRE(!filesynccomplete(&work, request, &io, 1) && txn.journal.durable &&
              filesyncstep(&work, request, RQ_CHECK_LIVE, &map, NULL, syncsubmit, &io) == 1 &&
              !work.request && entry.dirty && entry.data[0] == later && txn.journal.last == 47 &&
              txnpreparefile(&txn, &map, NULL, snapshot, 1) == 1 &&
              !txnstart(&txn, NULL, NULL, NULL) && !txnnext(&txn, &io) && !txnstop(&txn) &&
              txnrelease(&txn) == -ERR_WOULD_BLOCK && !txncomplete(&txn, &io, 0) &&
              !txnrelease(&txn) && entry.dirty &&
              txnprepare(&txn, NULL, 0, &key, 1, snapshot) == -ERR_STATE && !cacheput(&cache, &key),
          263);
}

static Vdone waitblock(Blkqueue *, u64);

static void
journaltransfer(Blkqueue *queue, Handle binding, u64 irqevent, const Journalio *io)
{
  u32   type = io->kind == JOURNAL_WRITE ? VBLK_WRITE : VBLK_FLUSH;
  u32   bytes = type == VBLK_WRITE ? FS_BLOCK_BYTES : 0;
  u64   alarm, started = now();
  Vdone done;
  REQUIRE(started <= UINT64_MAX - VDEV_RESET_NS &&
              !__sys_alarmset(started + VDEV_RESET_NS, &alarm) && !__sys_irqack(binding) &&
              !blkqsubmitbytes(queue, 0, type, io->block * FS_BLOCK_BYTES, bytes, io->bytes,
                               blksync, queue->device),
          268);
  done = waitblock(queue, irqevent);
  REQUIRE(!blkqfinish(queue, &done, NULL, 0) && !__sys_alarmcancel(&alarm), 269);
  vdev_trace[type == VBLK_WRITE ? 2 : 3]++;
}

static void
testtruncate(Blkqueue *queue, Handle binding, u64 irqevent, Journal *journal, Cache *cache,
             Loadmap *map, Blockmap *committed, Blockmap *prepared, u8 records[][FS_BLOCK_BYTES])
{
  Loadmap   staged;
  Shrink    shrink = {0};
  Reclaimtx work = {0};
  Reclaim   batch;
  Blockmap  busy = {0};
  Journalio io;
  Mapmeta   meta = {records[0], NULL, NULL, records[2], records[3], records[4]};
  u32       i, step;
  u32       offset = 2 * FS_INODE_BYTES;
  u64       before = committed->used[1], length = map->file.length;
  /* Test inode image: retain the direct pointer in the size transaction. */
  for (i = 0; i < FS_BLOCK_BYTES; i++)
    records[0][i] = 0;
  records[0][offset] = 1;
  records[0][offset + 76] = 100;
  REQUIRE(!loadmapshrink(map, 0, &staged) &&
              !journalshrink(&shrink, journal, cache, map, &staged, &meta),
          286);
  for (step = 0; journal->phase != JOURNAL_DONE; step++) {
    REQUIRE(step < 10 && !journalnext(journal, &io) && io.txid == 49 &&
                shrinkpublish(&shrink) == -ERR_WOULD_BLOCK && map->file.length == length &&
                committed->used[1] == before,
            287);
    journaltransfer(queue, binding, irqevent, &io);
    REQUIRE(!journalcomplete(journal, &io, 1), 288);
  }
  REQUIRE(step == 10 && !shrinkpublish(&shrink) && !map->file.length && map->direct[1] == 100 &&
              committed->used[1] == before && journal->last == 49 &&
              loadreclaim(map, NULL, FS_DATA_START, (u32) journal->blocks, &busy, prepared,
                          &batch) == 1 &&
              batch.count == 1 && batch.blocks[0] == 100,
          289);
  records[0][offset + 76] = 0;
  for (i = 0; i < FS_BLOCK_BYTES; i++)
    records[1][i] = (u8) (prepared->used[i / 8] >> ((i % 8) * 8));
  meta.bitmap = records[1];
  REQUIRE(!journalreclaim(&work, journal, cache, &batch, map, NULL, committed, &meta), 290);
  for (step = 0; journal->phase != JOURNAL_DONE; step++) {
    REQUIRE(step < 12 && !journalnext(journal, &io) && io.txid == 50 &&
                reclaimpublish(&work) == -ERR_WOULD_BLOCK && map->direct[1] == 100 &&
                committed->used[1] == before,
            291);
    journaltransfer(queue, binding, irqevent, &io);
    REQUIRE(!journalcomplete(journal, &io, 1), 292);
  }
  REQUIRE(step == 12 && !reclaimpublish(&work) && !map->direct[1] && journal->last == 50 &&
              committed->used[1] == (UINT64_C(1) << (FS_DATA_START - 64)),
          293);
}

static void
testgrowth(Blkqueue *queue, Handle binding, u64 irqevent)
{
  static Journal     journal;
  static Cacheblock  entry;
  static Writeupdate update;
  static Writeimage  image;
  static Blockmap    committed, reserved;
  static u8          records[5][FS_BLOCK_BYTES];
  Loadmap            map = {{2, 1, 1, 0}, {0}, 0};
  Growth             work = {0};
  Cache              cache;
  Journalio          io;
  Mapmeta            meta = {records[0], records[1], NULL, records[2], records[3], records[4]};
  u32                i, j, step = 0;
  u32                blocks = (u32) (queue->config.sectors / (FS_BLOCK_BYTES / VBLK_SECTOR));
  committed.used[1] = UINT64_C(1) << (FS_DATA_START - 64);
  reserved = committed;
  for (i = 0; i < ARRAY_SIZE(records); i++)
    for (j = 0; j < FS_BLOCK_BYTES; j++)
      records[i][j] = (u8) (0xc1 + i * 7 + j * 19);
  REQUIRE(!cacheinit(&cache, &entry, 1, blocks) && !journalinit(&journal, blocks, 47) &&
              !loadwritealloc(&map, NULL, FS_DATA_START, blocks, FS_BLOCK_BYTES + 3, 2, &reserved,
                              &update) &&
              !loadwritestage(&update.plan, FS_DATA_START, blocks, update.map.direct[1], NULL, NULL,
                              "xy", &image) &&
              !journalgrowth(&work, &journal, &cache, &update, &image, &meta),
          264);
  while (journal.phase != JOURNAL_DONE) {
    u32 type, bytes;
    REQUIRE(!journalnext(&journal, &io) && io.txid == 48 && !map.file.length &&
                growthpublish(&work, &map, NULL, &committed, &reserved) == -ERR_WOULD_BLOCK,
            265);
    type = io.kind == JOURNAL_WRITE ? VBLK_WRITE : VBLK_FLUSH;
    bytes = type == VBLK_WRITE ? FS_BLOCK_BYTES : 0;
    REQUIRE(blkqsubmitbytes(queue, 0, type, 1, bytes, io.bytes, blksync, queue->device) ==
                -ERR_INVALID,
            268);
    journaltransfer(queue, binding, irqevent, &io);
    REQUIRE(!journalcomplete(&journal, &io, 1) && ++step <= 13, 266);
  }
  REQUIRE(step == 13 && !growthpublish(&work, &map, NULL, &committed, &reserved) &&
              map.file.length == FS_BLOCK_BYTES + 5 && map.direct[1] == 100 &&
              committed.used[1] == reserved.used[1] && journal.last == 48,
          267);
  testtruncate(queue, binding, irqevent, &journal, &cache, &map, &committed, &reserved, records);
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
resetpending(Blkqueue *queue, Handle binding, u64 irqevent, const Vblkconfig *config)
{
  Vdev     *device = queue->device;
  Vring    *ring = queue->ring;
  Vblkreq  *request = queue->slots;
  Resetsync sync = {device, 0, 0}, fault = {device, 0, 1};
  Ipcmsg    event;
  Vdone     done[VQ_BATCH];
  u64       generation, started = now();
  u32       causes, count, more;
  int       error;

  REQUIRE(started <= UINT64_MAX - VDEV_RESET_NS &&
              !__sys_alarmset(started + VDEV_RESET_NS, &generation) && !__sys_irqack(binding),
          185);
  REQUIRE(!blkqsubmit(queue, 0, VBLK_READ, config->blocksize / VBLK_SECTOR, config->blocksize, NULL,
                      resetsync, &sync),
          186);
  REQUIRE(!ipc_recv(irqevent | EVENT_TIMER, RECV_EVENTS_ONLY, &event) && event.kind == IPC_EVENT &&
              event.words[0] == irqevent && !__sys_alarmcancel(&generation),
          187);
  /* Leave the device cause and used entry unconsumed. The IRQ alone does
     not return ownership of the request buffers. */
  REQUIRE(!virtioread(device->ops.context, 0x60, &causes) && causes == VDEV_IRQ_USED &&
              request->active && request->held == 7 && ring->queue.free == VQ_SIZE - 3,
          188);
  REQUIRE(blkqsubmit(queue, 1, VBLK_READ, 0, config->blocksize, NULL, resetsync, &fault) ==
                  -ERR_FAULT &&
              !request[1].active && request[1].held == 1 && !fault.after &&
              blkqsubmit(queue, 1, VBLK_READ, 0, config->blocksize, NULL, blksync, device) ==
                  -ERR_STATE &&
              blkqpoll(queue, done, &count, &more, &causes) == -ERR_STATE && !count && !more &&
              !causes && !virtioread(device->ops.context, 0x60, &causes) && causes == VDEV_IRQ_USED,
          236);
  do {
    error = blkqrecover(queue, now());
  } while (error == -ERR_WOULD_BLOCK);
  REQUIRE(error == -ERR_BUSY && sync.after == 2 && request->held == 6 &&
              blkqinit(queue, device, ring, request, queue->buffers, queue->count) == -ERR_STATE &&
              blkqsubmit(queue, 0, VBLK_READ, 0, config->blocksize, NULL, blksync, device) ==
                  -ERR_STATE,
          229);
  REQUIRE(!virtioread(device->ops.context, 0x60, &causes) && !causes &&
              !blkqrecover(queue, now()) && sync.after == 4 && !request->active && !request->held &&
              fault.after == 1 && !request[1].held &&
              blkqinit(queue, device, ring, request, queue->buffers, queue->count) == -ERR_STALE,
          189);
  vdev_trace[16]++;
  vdev_trace[39]++;
}
static void
testresize(Blkqueue *queue, Handle binding, u64 irqevent, const Vblkconfig *old)
{
  static u8  staged[PAGE_SIZE];
  Vdev      *device = queue->device;
  Vblkconfig refreshed;
  Ipcmsg     event;
  Vdone      done[VQ_BATCH];
  u32        count, more, causes, i;
  u64        alarm, started = now();
  REQUIRE(blkqsubmit(queue, 0, VBLK_READ, old->sectors, old->blocksize, NULL, blksync, device) ==
              -ERR_INVALID,
          231);
  REQUIRE(started <= UINT64_MAX - VDEV_RESET_NS &&
              !__sys_alarmset(started + VDEV_RESET_NS, &alarm) && !__sys_irqack(binding),
          208);
  vdev_trace[VTEST_RESIZE] = 1;
  for (;;) {
    REQUIRE(!ipc_recv(irqevent | EVENT_TIMER, RECV_EVENTS_ONLY, &event) &&
                event.kind == IPC_EVENT && event.words[0] == irqevent,
            209);
    REQUIRE(!blkqpoll(queue, done, &count, &more, &causes) && !count && !more, 211);
    if (causes & VDEV_IRQ_CONFIG)
      break;
    REQUIRE(!__sys_irqack(binding), 226);
  }
  REQUIRE(!blkqpoll(queue, done, &count, &more, &causes) && !count && !more && !causes &&
              blkqsubmit(queue, 0, VBLK_READ, 0, old->blocksize, NULL, blksync, device) ==
                  -ERR_WOULD_BLOCK,
          213);
  REQUIRE(!blkqrefresh(queue, &refreshed) && refreshed.sectors == 2 * old->sectors &&
              refreshed.blocksize == old->blocksize && refreshed.physical == old->physical &&
              refreshed.alignment == old->alignment && refreshed.flush == old->flush &&
              refreshed.readonly == old->readonly,
          212);
  REQUIRE(!__sys_irqack(binding) && !blkqsubmit(queue, 0, VBLK_READ, old->sectors,
                                                refreshed.blocksize, NULL, blksync, device),
          232);
  for (;;) {
    REQUIRE(!ipc_recv(irqevent | EVENT_TIMER, RECV_EVENTS_ONLY, &event) &&
                event.kind == IPC_EVENT && event.words[0] == irqevent &&
                !blkqpoll(queue, done, &count, &more, &causes) && !more &&
                !(causes & VDEV_IRQ_CONFIG),
            233);
    if (count)
      break;
    REQUIRE(!__sys_irqack(binding), 234);
  }
  REQUIRE(!__sys_alarmcancel(&alarm) && count == 1 &&
              !blkqfinish(queue, &done[0], staged, sizeof(staged)),
          227);
  for (i = 0; i < refreshed.blocksize; i++)
    REQUIRE(staged[i] == 0, 235);
  vdev_trace[VTEST_CAPACITY] = refreshed.sectors;
}
static void
testparallel(Blkqueue *queue, Handle binding, u64 irqevent, const Vblkconfig *config)
{
  static Cacheblock storage[2];
  Cache             cache = {0};
  Cachespan         span = {0};
  Cachefill         fills[2] = {{0}};
  Cacheio           tickets[2], duplicate;
  Cachekey          competing = {0};
  u8               *destinations[2], *unused = NULL;
  u8                staged[4] = {0xa5, 0xa5, 0xa5, 0xa5};
  u8                transfer[CACHE_BYTES];
  Vdev             *device = queue->device;
  Ipcmsg            event;
  Vdone             done[VQ_BATCH], stale;
  u64               stride = config->blocksize / VBLK_SECTOR;
  u64               alarm, started = now();
  u32               count, causes, more, i, j, seen = 0;
  int               slot;
  REQUIRE(
      config->blocksize == CACHE_BYTES &&
          !cacheinit(&cache, storage, ARRAY_SIZE(storage), config->sectors / stride) &&
          !cachespanbegin(&cache, 1, 2, CACHE_BYTES - 2, sizeof(staged), &span) &&
          cachefillstart(&fills[0], &cache, 1, CACHE_DATA, &tickets[0], &destinations[0]) == 1 &&
          cachefillstart(&fills[1], &cache, 2, CACHE_DATA, &tickets[1], &destinations[1]) == 1 &&
          cachefetch(&cache, 1, CACHE_DATA, &duplicate, &unused) == -ERR_WOULD_BLOCK && !unused,
      237);
  REQUIRE(started <= UINT64_MAX - VDEV_RESET_NS &&
              !__sys_alarmset(started + VDEV_RESET_NS, &alarm) && !__sys_irqack(binding),
          215);
  REQUIRE(
      !blkqsubmit(queue, 0, VBLK_READ, stride, config->blocksize, NULL, blksync, device) &&
          !blkqsubmit(queue, 1, VBLK_READ, 2 * stride, config->blocksize, NULL, blksync, device) &&
          blkqsubmit(queue, 0, VBLK_READ, 0, config->blocksize, NULL, blksync, device) == -ERR_BUSY,
      216);
  while (seen != 3) {
    REQUIRE(!ipc_recv(irqevent | EVENT_TIMER, RECV_EVENTS_ONLY, &event) &&
                event.kind == IPC_EVENT && event.words[0] == irqevent,
            217);
    REQUIRE(!blkqpoll(queue, done, &count, &more, &causes) && !(causes & VDEV_IRQ_CONFIG) && !more,
            218);
    for (i = 0; i < count; i++) {
      slot = blkqmatch(queue, &done[i]);
      REQUIRE(slot >= 0 && slot < 2 && !(seen & (1U << slot)), 219);
      stale = done[i];
      stale.generation--;
      REQUIRE(blkqfinish(queue, &stale, destinations[slot], CACHE_BYTES) == -ERR_STALE, 225);
      stale = done[i];
      stale.cookie += VQ_SIZE;
      REQUIRE(blkqmatch(queue, &stale) == -ERR_STALE &&
                  !blkqfinish(queue, &done[i], transfer, sizeof(transfer)) &&
                  blkqmatch(queue, &done[i]) == -ERR_STALE &&
                  cachefillcopy(&fills[slot], &tickets[slot ^ 1], transfer, sizeof(transfer)) ==
                      -ERR_STALE &&
                  cachefillcopy(&fills[slot], &tickets[slot], transfer, CACHE_BYTES - 1) ==
                      -ERR_INVALID &&
                  cachespancopy(&span, staged) == -ERR_WOULD_BLOCK &&
                  !cachefillcopy(&fills[slot], &tickets[slot], transfer, sizeof(transfer)) &&
                  cachefillcopy(&fills[slot], &tickets[slot], transfer, sizeof(transfer)) ==
                      -ERR_STALE,
              220);
      for (j = 0; j < config->blocksize; j++)
        REQUIRE(destinations[slot][j] == (u8) ((slot ? 0x37 : 0x51) + j * 37), 221);
      seen |= 1U << slot;
      if (seen != 3) {
        REQUIRE(cachespancopy(&span, staged) == -ERR_WOULD_BLOCK &&
                    cacheget(&cache, 3, CACHE_DATA, &competing) == -ERR_CAPACITY,
                238);
        for (j = 0; j < sizeof(staged); j++)
          REQUIRE(staged[j] == 0xa5, 239);
      }
    }
    if (seen != 3)
      REQUIRE(!__sys_irqack(binding), 222);
  }
  REQUIRE(!__sys_alarmcancel(&alarm) && queue->ring->queue.free == VQ_SIZE, 223);
  REQUIRE(!cachespancopy(&span, staged) && staged[0] == (u8) (0x51 + (CACHE_BYTES - 2) * 37) &&
              staged[1] == (u8) (0x51 + (CACHE_BYTES - 1) * 37) && staged[2] == 0x37 &&
              staged[3] == (u8) (0x37 + 37) && !cachespanend(&span) &&
              !cacheget(&cache, 3, CACHE_DATA, &competing) && !cacheput(&cache, &competing),
          240);
  vdev_trace[VTEST_PARALLEL] = 2;
}
static Vdone
waitblock(Blkqueue *queue, u64 irqevent)
{
  Vdone  done[VQ_BATCH];
  Ipcmsg event;
  u32    count, more, causes;
  REQUIRE(!ipc_recv(irqevent | EVENT_TIMER, RECV_EVENTS_ONLY, &event) && event.kind == IPC_EVENT &&
              event.words[0] == irqevent && !blkqpoll(queue, done, &count, &more, &causes) &&
              count == 1 && !more && causes == VDEV_IRQ_USED,
          241);
  vdev_trace[38]++;
  return done[0];
}
static void
testflush(Blkqueue *queue, Handle binding, u64 irqevent, u64 sector)
{
  static u8 source[PAGE_SIZE];
  Vdev     *device = queue->device;
  Vdone     done;
  u64       alarm, started = now();
  u32       i;
  for (i = 0; i < sizeof(source); i++)
    source[i] = (u8) (0x37 + i * 37);
  REQUIRE(started <= UINT64_MAX - VDEV_RESET_NS &&
              !__sys_alarmset(started + VDEV_RESET_NS, &alarm) && !__sys_irqack(binding) &&
              !blkqsubmit(queue, 0, VBLK_WRITE, sector, sizeof(source), source, blksync, device),
          242);
  done = waitblock(queue, irqevent);
  REQUIRE(blkqsubmit(queue, 1, VBLK_FLUSH, 0, 0, NULL, blksync, device) == -ERR_WOULD_BLOCK &&
              !blkqfinish(queue, &done, NULL, 0) && !__sys_irqack(binding) &&
              !blkqsubmit(queue, 1, VBLK_FLUSH, 0, 0, NULL, blksync, device),
          243);
  done = waitblock(queue, irqevent);
  REQUIRE(blkqsubmit(queue, 0, VBLK_WRITE, sector, sizeof(source), source, blksync, device) ==
                  -ERR_WOULD_BLOCK &&
              blkqsubmit(queue, 0, VBLK_FLUSH, 0, 0, NULL, blksync, device) == -ERR_WOULD_BLOCK &&
              !blkqfinish(queue, &done, NULL, 0) && !__sys_alarmcancel(&alarm),
          244);
  vdev_trace[2]++;
  vdev_trace[3]++;
}
static void
testblock(Vdev *device, const Vblkconfig *config, u32 member)
{
  Vring      ring;
  Vdone      stale[VQ_BATCH];
  Vdone      prior = {0};
  Blkqueue   queue = {0};
  Vblkreq    requests[2];
  Blkdma     buffers[2];
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
  testbuffer(&dma, 3, &buffers[0]);
  testbuffer(&dma, 6, &buffers[1]);
  REQUIRE(!blkqinit(&queue, device, &ring, requests, buffers, ARRAY_SIZE(requests)), 224);
  if (!config->readonly) {
    testflush(&queue, binding, irqevent, 2 * stride);
    transfer(device, &ring, 1, binding, irqevent, config, &dma, VBLK_READ, 2 * stride, 4, 0x37,
             NULL, NULL);
  }
  resetpending(&queue, binding, irqevent, config);
  REQUIRE(!vblkfeatures(device) && !vblkconfig(device, &fresh) &&
              fresh.sectors == config->sectors && fresh.readonly == config->readonly &&
              fresh.flush == config->flush && fresh.blocksize == config->blocksize &&
              fresh.physical == config->physical && fresh.alignment == config->alignment,
          183);
  startqueue(device, &ring, 2, &dma);
  REQUIRE(!blkqinit(&queue, device, &ring, requests, buffers, ARRAY_SIZE(requests)) &&
              blkqmatch(&queue, &prior) == -ERR_STALE,
          230);
  REQUIRE(vrpoll(&ring, 1, stale, &count, &more) == -ERR_STALE && !count && !more, 184);
  REQUIRE(prior.generation == 1, 191);
  transfer(device, &ring, 2, binding, irqevent, &fresh, &dma, VBLK_READ,
           config->readonly ? stride : 2 * stride, 1, config->readonly ? 0xa7 : 0x37, &prior, NULL);
  REQUIRE(prior.generation == 2, 192);
  if (!config->readonly) {
    testredo(device, &ring, binding, irqevent, &fresh, &dma);
    testwriteback(device, &ring, binding, irqevent, &fresh, &dma);
    testparallel(&queue, binding, irqevent, &fresh);
    testgrowth(&queue, binding, irqevent);
    testresize(&queue, binding, irqevent, &fresh);
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
