#include <unistd.h>
#include <errno.h>
#include <string.h>
#include <sys/channel.h>
#include <abi/time.h>
#include <abi/syscall.h>
#include <abi/irq.h>
#include "private/syscall.h"
#include "private/start.h"
#include "../../sbin/blk/serve.h"
#include "private/virtio/mmio.h"
#include "../../sbin/fs/blklog.h"
#include "../../sbin/fs/blkread.h"
#include "../../sbin/fs/context.h"
#include "../../sbin/fs/mount.h"
#include "private/bootstrap.h"
#include "../../sbin/fs/cache.h"
#include "../../sbin/fs/crc.h"
#include "../../sbin/fs/load.h"
#include "../../sbin/fs/sync.h"
#include "../../sbin/fs/walk.h"
#include "virtiouser.h"
volatile u64             vdev_trace[VTEST_TRACE];
static u32               ringafter[3], failringafter;
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
prepareblk(Vdev *device, Vblkconfig *config)
{
  int error;
  do {
    error = blkqprepare(device, now(), config);
  } while (error == -ERR_WOULD_BLOCK);
  return error;
}
static int
coherentsync(void *context, u32 region, u32 phase, u64 offset, u64 length)
{
  Vdev *device = context;
  if (phase == VR_AFTER && !offset && length == PAGE_SIZE) {
    REQUIRE(region < ARRAY_SIZE(ringafter) && device->state == VDEV_RESET && !device->status, 467);
    ringafter[region]++;
    if (region == VR_AVAIL && failringafter) {
      failringafter = 0;
      return -ERR_BUSY;
    }
  }
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
typedef struct Journaltransport Journaltransport;
struct Journaltransport
{
  Blkqueue *queue;
  Handle    binding;
  u64       event, alarm;
  Journalio ticket;
  Journal  *journal;
};
static isize journalsubmit(void *, const Journalio *, u64, u32);
static void  journaldrain(Journaltransport *, Journal *);

static void
testroot(u32 blocks)
{
  static Blockmap        allocation;
  static Loadowners      owners;
  static const Loadindex index = {FS_DATA_START, {0}};
  Loadmap                map = {{FS_ROOT, 1, 1, FS_BLOCK_BYTES}, {FS_DATA_START}, 0};
  Loadmap                orphan = {{FS_ROOT + 1, 1, 1, 0}, {FS_DATA_START + 1, FS_DATA_START}, 0};
  REQUIRE(loadrootcheck(1, VNODE_DIR, 1, &map, NULL, &allocation, blocks) == -ERR_STATE, 330);
  allocation.used[FS_DATA_START / 64] |= UINT64_C(1) << (FS_DATA_START % 64);
  REQUIRE(!loadrootcheck(1, VNODE_DIR, 1, &map, NULL, &allocation, blocks) &&
              loadrootcheck(1, VNODE_FILE, 1, &map, NULL, &allocation, blocks) == -ERR_STATE &&
              loadrootcheck(1, VNODE_DIR, 0, &map, NULL, &allocation, blocks) == -ERR_STATE,
          331);
  map.indirect = FS_DATA_START + 1;
  allocation.used[map.indirect / 64] |= UINT64_C(1) << (map.indirect % 64);
  REQUIRE(loadrootcheck(1, VNODE_DIR, 1, &map, NULL, &allocation, blocks) == -ERR_WOULD_BLOCK, 332);
  map.indirect = 0;
  map.direct[1] = FS_DATA_START;
  REQUIRE(loadrootcheck(1, VNODE_DIR, 1, &map, NULL, &allocation, blocks) == -ERR_STATE, 333);
  map.direct[1] = 0;
  REQUIRE(!loadmapclaim(FS_ROOT, VNODE_DIR, 1, &map, NULL, &allocation, blocks, &owners) &&
              loadmapclaim(FS_ROOT + 1, VNODE_FILE, 0, &orphan, NULL, &allocation, blocks,
                           &owners) == -ERR_STATE &&
              owners.owned.used[FS_DATA_START / 64] == (UINT64_C(1) << (FS_DATA_START % 64)),
          334);
  orphan.direct[1] = 0;
  REQUIRE(!loadmapclaim(FS_ROOT + 1, VNODE_FILE, 0, &orphan, NULL, &allocation, blocks, &owners) &&
              owners.owned.used[FS_DATA_START / 64] == allocation.used[FS_DATA_START / 64],
          335);
  orphan.direct[0] = 0;
  orphan.indirect = FS_DATA_START;
  orphan.file.inode++;
  REQUIRE(loadmapclaim(FS_ROOT + 2, VNODE_FILE, 0, &orphan, NULL, &allocation, blocks, &owners) ==
                  -ERR_WOULD_BLOCK &&
              loadmapclaim(FS_ROOT + 2, VNODE_FILE, 0, &orphan, &index, &allocation, blocks,
                           &owners) == -ERR_STATE &&
              owners.owned.used[FS_DATA_START / 64] == allocation.used[FS_DATA_START / 64],
          336);
  orphan.file.inode--;
  orphan.file.version++;
  REQUIRE(loadmapclaim(FS_ROOT + 2, VNODE_FILE, FS_INODES + 1, &orphan, NULL, &allocation, blocks,
                       &owners) == -ERR_STATE,
          360);
}

typedef struct Redotest Redotest;
struct Redotest
{
  const u8 *bitmap;
  u32       waiting, bad;
};

static isize
redoload(void *context, const Redoscan *scan, u32 ino, Redoinode *out)
{
  static const Loadmap   root = {{FS_ROOT, 1, 1, FS_BLOCK_BYTES}, {109}, 0};
  static const Loadmap   file = {{FS_ROOT + 1, 1, 1, FS_BLOCK_BYTES}, {99}, 108};
  static const Loadindex index = {108, {0}};
  Redotest              *test = context;
  const u8              *bytes;
  if (journalview(scan->blocks, scan->images, scan->count, FS_INODE_MAP, &bytes) ||
      bytes != test->bitmap)
    return -ERR_STATE;
  if (ino == FS_ROOT) {
    *out = (Redoinode){&root, NULL, VNODE_DIR, 1};
    return 0;
  }
  if (ino != FS_ROOT + 1 || test->bad)
    return -ERR_STATE;
  *out = (Redoinode){&file, test->waiting ? NULL : &index, VNODE_FILE, 1};
  test->waiting = 0;
  return 0;
}

static void
testredo(Blkqueue *queue, Handle binding, u64 irqevent, const Vblkconfig *config)
{
  static Journal        journal, idle;
  static Redoscan       scan, broken;
  static Loadowners     owners, partial;
  static Blockmap       allocation;
  static const Inodemap inodes = {{6}};
  static u8             images[3][JOURNAL_BYTES];
  Redotest              fixture = {images[0], 1, 0};
  Journalblock          metadata[2];
  Journalio             io;
  Journaltransport      transport = {queue, binding, irqevent, 0, {0}, &journal};
  Fsgeometry            geometry;
  u32                   i, j, count = 0;
  u64                   stride = JOURNAL_BYTES / VBLK_SECTOR;
  u64                   blocks = config->sectors / stride;
  Fslayout              layout = {blocks, 1, 64, 65, 66, 67, 99, 1, 4096, 1024, 1};
  Journalmark           mark = {47, 0, 0, 0};
  const Journalmark     fresh = {0, 0, 0, 0};
  Journalhead           head = {47, {65, 66}, 1, 2, 11, 12};
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
  REQUIRE(journalrecover(&journal, &layout, &geometry, &mark, NULL, NULL, 0, 0, NULL, NULL) ==
                  -ERR_STATE &&
              !journal.last,
          249);
  REQUIRE(journalreadonly(&layout, &geometry, &mark, NULL) == -ERR_STATE, 245);
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
  geometry.readonly = 1;
  geometry.faultbytes = 0;
  REQUIRE(journalreadonly(&layout, &geometry, &mark, NULL) == -ERR_WOULD_BLOCK &&
              journalreadonly(&layout, &geometry, &fresh, NULL) == -ERR_WOULD_BLOCK &&
              journalreadonly(&layout, &geometry, NULL, NULL) == -ERR_STATE &&
              journalrecover(&journal, &layout, &geometry, &mark, NULL, NULL, 0, 0, NULL, NULL) ==
                  -ERR_NOTSUP &&
              !journal.last,
          250);
  {
    const Journalmark committed = {47, 1, 11, 12}, unknown = {47, 2, 0, 0};
    const Journalmark bad = {47, 0, 11, 0};
    REQUIRE(journalreadonly(&layout, &geometry, &committed, NULL) == -ERR_STATE &&
                journalreadonly(&layout, &geometry, &unknown, NULL) == -ERR_STATE &&
                journalreadonly(&layout, &geometry, &bad, NULL) == -ERR_STATE,
            250);
  }
  geometry.readonly = config->readonly;
  geometry.faultbytes = FS_BLOCK_BYTES;
  geometry.flush = config->flush;
  {
    Journalblock data = {FS_DATA_START, images[0]};
    Journalplan  plan = {NULL, NULL, NULL, NULL, &data, 0, 1};
    REQUIRE(!journalrecover(&idle, &layout, &geometry, &fresh, NULL, NULL, 0, 0, NULL, NULL) &&
                !journalbegin(&idle, &plan) && !idle.txid && !journalstop(&idle) &&
                journalrecover(&idle, &layout, &geometry, &fresh, NULL, NULL, 0, 0, NULL, NULL) ==
                    -ERR_STATE &&
                idle.data_count == 1 && idle.data[0].bytes == images[0] && !journalrelease(&idle),
            246);
  }
  REQUIRE(!journalrecover(&idle, &layout, &geometry, &mark, NULL, NULL, 0, 0, NULL, NULL) &&
              idle.last == 47 && journalnext(&idle, &io) == -ERR_STATE &&
              journalrecover(&idle, &layout, &geometry, &mark, NULL, NULL, 0, 0, NULL, NULL) ==
                  -ERR_STATE,
          246);
  mark = (Journalmark){47, 1, 11, 12};
  {
    const Journalmark retired = {47, 0, 0, 0};
    Journalblock      data = {FS_DATA_START, images[0]};
    Journalplan       plan = {NULL, NULL, NULL, NULL, &data, 0, 1};
    REQUIRE(
        !journalbegin(&idle, &plan) && !journalstop(&idle) && !journalrelease(&idle) &&
            journalrecover(&idle, &layout, &geometry, &fresh, NULL, NULL, 0, 0, NULL, NULL) ==
                -ERR_STATE &&
            journalrecover(&idle, &layout, &geometry, &mark, &head, metadata, 2, 12, images[2],
                           NULL) == -ERR_STATE &&
            idle.last == 47 && idle.phase == JOURNAL_STOPPED &&
            !journalrecover(&idle, &layout, &geometry, &retired, NULL, NULL, 0, 0, NULL, NULL) &&
            idle.last == 47,
        246);
  }
  REQUIRE(journalrecover(&journal, &layout, &geometry, &mark, &head, metadata, 2, 13, images[2],
                         NULL) == -ERR_STATE &&
              journalnext(&journal, &io) == -ERR_STATE,
          247);
  head.targets[1] = metadata[1].block = 99;
  REQUIRE(journalrecover(&journal, &layout, &geometry, &mark, &head, metadata, 2, 12, images[2],
                         NULL) == -ERR_STATE &&
              journalnext(&journal, &io) == -ERR_STATE,
          248);
  head.targets[1] = metadata[1].block = 109;
  {
    const u8 *bytes = NULL;
    REQUIRE(!journalview(blocks, metadata, 2, 109, &bytes) && bytes == images[1] &&
                journalview(blocks, metadata, 2, FS_LOG_START, &bytes) == -ERR_INVALID &&
                bytes == images[1],
            340);
    metadata[0].block = 109;
    REQUIRE(journalview(blocks, metadata, 2, 109, &bytes) == -ERR_STATE && bytes == images[1], 341);
    metadata[0].block = FS_INODE_MAP;
    REQUIRE(!journalview(blocks, metadata, 2, FS_INODE_MAP, &bytes) && bytes == images[0] &&
                !journalview(blocks, metadata, 2, FS_BLOCK_MAP, &bytes) && !bytes,
            342);
  }
  allocation.used[1] = (UINT64_C(1) << (99 - 64)) | (UINT64_C(3) << (108 - 64));
  REQUIRE(!redoscaninit(&scan, (u32) blocks, metadata, 2, &inodes, &allocation, &owners) &&
              redoscanstep(&scan, redoload, &fixture) == -ERR_WOULD_BLOCK && scan.next == 2 &&
              !journalmetadata(&scan, metadata, 2, 1),
          337);
  for (i = 0; i < FS_INODES / 32; i++)
    REQUIRE(redoscanstep(&scan, redoload, &fixture) == (i == FS_INODES / 32 - 1) &&
                scan.next == (i + 1) * 32,
            338);
  REQUIRE(journalmetadata(&scan, metadata, 2, 1), 339);
  metadata[1].block = 99;
  REQUIRE(!journalmetadata(&scan, metadata, 2, 1), 343);
  metadata[1].block = 109;
  fixture.bad = 1;
  REQUIRE(!redoscaninit(&broken, (u32) blocks, metadata, 2, &inodes, &allocation, &partial) &&
              redoscanstep(&broken, redoload, &fixture) == -ERR_STATE,
          344);
  fixture.bad = 0;
  REQUIRE(redoscanstep(&broken, redoload, &fixture) == -ERR_STATE &&
              !journalmetadata(&broken, metadata, 2, 1),
          345);
  {
    static Redoscan   leaked;
    static Loadowners unowned;
    static Blockmap   leaks;
    /* Last in-volume bit, to cover the upper boundary of the bitmap. */
    u32 block = (u32) blocks - 1;
    u64 bit = UINT64_C(1) << (block % 64);
    REQUIRE(block > 109 && !(allocation.used[block / 64] & bit), 346);
    leaks.used[1] = allocation.used[1];
    leaks.used[block / 64] |= bit;
    REQUIRE(!redoscaninit(&leaked, (u32) blocks, metadata, 2, &inodes, &leaks, &unowned), 347);
    for (i = 0; i < FS_INODES / 32; i++)
      REQUIRE(redoscanstep(&leaked, redoload, &fixture) ==
                  (i == FS_INODES / 32 - 1 ? -ERR_STATE : 0),
              348);
    REQUIRE(journalrecover(&journal, &layout, &geometry, &mark, &head, metadata, 2, 12, images[2],
                           &leaked) == -ERR_STATE &&
                journalnext(&journal, &io) == -ERR_STATE,
            349);
    REQUIRE(redoscanstep(&leaked, redoload, &fixture) == -ERR_STATE, 350);
  }
  geometry.faultbytes = 0;
  REQUIRE(journalrecover(&journal, &layout, &geometry, &mark, &head, metadata, 2, 12, images[2],
                         &scan) == -ERR_NOTSUP &&
              !journal.last && journalnext(&journal, &io) == -ERR_STATE,
          251);
  geometry.faultbytes = FS_BLOCK_BYTES;
  layout.blocks--;
  REQUIRE(!fslayoutcheck(&layout, &geometry, 1) &&
              journalrecover(&journal, &layout, &geometry, &mark, &head, metadata, 2, 12, images[2],
                             &scan) == -ERR_STATE &&
              journalnext(&journal, &io) == -ERR_STATE,
          354);
  layout.blocks = blocks;
  REQUIRE(!journalrecover(&journal, &layout, &geometry, &mark, &head, metadata, 2, 12, images[2],
                          &scan),
          194);
  while (journal.phase != JOURNAL_DONE) {
    REQUIRE(count < 5 && !journalpump(&journal, journalsubmit, &transport) &&
                transport.ticket.txid == 47 && journalrelease(&journal) == -ERR_STATE,
            195);
    journaldrain(&transport, &journal);
    count++;
  }
  REQUIRE(count == 5 && !journalrelease(&journal) && journal.last == 47, 197);
  {
    static Redoscan   home;
    static Loadowners homeowners;
    const Journalmark retired = {47, 0, 0, 0};
    Redotest          reader = {NULL, 1, 0};
    geometry.readonly = 1;
    geometry.flush = geometry.faultbytes = 0;
    REQUIRE(journalreadonly(&layout, &geometry, &retired, &scan) == -ERR_STATE &&
                !redoscaninit(&home, (u32) blocks, NULL, 0, &inodes, &allocation, &homeowners) &&
                redoscanstep(&home, redoload, &reader) == -ERR_WOULD_BLOCK &&
                journalreadonly(&layout, &geometry, &retired, &home) == -ERR_WOULD_BLOCK,
            357);
    for (i = 0; i < FS_INODES / 32; i++)
      REQUIRE(redoscanstep(&home, redoload, &reader) == (i == FS_INODES / 32 - 1), 358);
    REQUIRE(!journalreadonly(&layout, &geometry, &retired, &home) &&
                journalreadonly(&layout, &geometry, &mark, &home) == -ERR_STATE,
            359);
    home.blocks--;
    REQUIRE(journalreadonly(&layout, &geometry, &retired, &home) == -ERR_STATE, 391);
    home.blocks++;
    home.failed = -ERR_FAULT;
    REQUIRE(journalreadonly(&layout, &geometry, &retired, &home) == -ERR_FAULT, 392);
  }
  testroot((u32) blocks);
}
static isize
syncsubmit(void *context, Handle request, const Journalio *io, u64 offset, u32 bytes)
{
  Journalio *pending = context;
  (void) request;
  REQUIRE(io->kind == JOURNAL_WRITE
              ? offset == FS_DATA_START * FS_BLOCK_BYTES && bytes == FS_BLOCK_BYTES
              : io->kind == JOURNAL_FLUSH && !offset && !bytes,
          298);
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

static Vdone waitblock(Blkqueue *, Handle, u64);

static isize
journalsubmit(void *context, const Journalio *io, u64 offset, u32 bytes)
{
  Journaltransport *transport = context;
  Blkqueue         *queue = transport->queue;
  u64               started = now();
  u32               type = io->kind == JOURNAL_WRITE ? VBLK_WRITE : VBLK_FLUSH;
  Journalio         stale = *io;
  u64               bad_off = UINT64_MAX;
  u32               bad_len = UINT32_MAX;
  stale.serial--;
  REQUIRE(journalrange(transport->journal, &stale, &bad_off, &bad_len) == -ERR_STALE &&
              bad_off == UINT64_MAX && bad_len == UINT32_MAX && !transport->ticket.serial &&
              started <= UINT64_MAX - VDEV_RESET_NS &&
              blkqsubmitbytes(queue, 0, type, 1, bytes, io->bytes, blksync, queue->device) ==
                  -ERR_INVALID &&
              !__sys_alarmset(started + VDEV_RESET_NS, &transport->alarm) &&
              !__sys_irqack(transport->binding) &&
              !blkqsubmitbytes(queue, 0, type, offset, bytes, io->bytes, blksync, queue->device),
          295);
  transport->ticket = *io;
  return 0;
}

static void
journaldrain(Journaltransport *transport, Journal *journal)
{
  Journalio *io = &transport->ticket;
  Vdone      done;
  REQUIRE(io->serial && !journalpump(journal, journalsubmit, transport), 296);
  done = waitblock(transport->queue, transport->binding, transport->event);
  REQUIRE(!blkqfinish(transport->queue, &done, NULL, 0) && !__sys_alarmcancel(&transport->alarm) &&
              !journalcomplete(journal, io, 1),
          297);
  vdev_trace[io->kind == JOURNAL_WRITE ? 2 : 3]++;
  io->serial = 0;
}

static void
testtruncate(Blkqueue *queue, Handle binding, u64 irqevent, Journal *journal, Cache *cache,
             Loadmap *map, Blockmap *committed, Blockmap *prepared, u8 records[][FS_BLOCK_BYTES])
{
  Loadmap          staged;
  Shrink           shrink = {0};
  Reclaimtx        work = {0};
  Reclaim          batch;
  Blockmap         busy = {0};
  Journaltransport transport = {queue, binding, irqevent, 0, {0}, journal};
  Mapmeta          meta = {records[0], NULL, NULL, records[2], records[3], records[4]};
  u32              i, step;
  u32              offset = 2 * FS_INODE_BYTES;
  u64              before = committed->used[1], length = map->file.length;
  /* Test inode image: retain the direct pointer in the size transaction. */
  for (i = 0; i < FS_BLOCK_BYTES; i++)
    records[0][i] = 0;
  records[0][offset] = 1;
  records[0][offset + 76] = 100;
  busy.used[1] = UINT64_C(1) << (100 - 64);
  REQUIRE(!loadmapshrink(map, 0, &staged) &&
              !journalshrink(&shrink, journal, cache, map, &staged, &meta),
          286);
  for (step = 0; journal->phase != JOURNAL_DONE; step++) {
    REQUIRE(step < 10 && !journalpump(journal, journalsubmit, &transport) &&
                transport.ticket.txid == 49 && shrinkpublish(&shrink) == -ERR_WOULD_BLOCK &&
                map->file.length == length && committed->used[1] == before,
            287);
    journaldrain(&transport, journal);
  }
  REQUIRE(step == 10 && !shrinkpublish(&shrink) && !map->file.length && map->direct[1] == 100 &&
              committed->used[1] == before && journal->last == 49 &&
              loadreclaimkind((u32) map->file.inode, 1, map, 1, 1) == RECLAIM_TAIL &&
              reclaimprepare(journal, cache, map, NULL, RECLAIM_TAIL, &busy, committed, prepared,
                             &batch) == -ERR_BUSY &&
              prepared->used[1] == before &&
              reclaimprepare(journal, cache, map, NULL, RECLAIM_TAIL, NULL, committed, prepared,
                             &batch) == 1 &&
              batch.count == 1 && batch.blocks[0] == 100,
          289);
  records[0][offset + 76] = 0;
  for (i = 0; i < FS_BLOCK_BYTES; i++)
    records[1][i] = (u8) (prepared->used[i / 8] >> ((i % 8) * 8));
  meta.bitmap = records[1];
  REQUIRE((fscrc32c(fscrc32c(UINT32_MAX, records[0], FS_BLOCK_BYTES), records[1], FS_BLOCK_BYTES) ^
           UINT32_MAX) == UINT32_C(0xbcec0885),
          294);
  REQUIRE(!journalreclaim(&work, journal, cache, &batch, map, NULL, committed, &meta), 290);
  for (step = 0; journal->phase != JOURNAL_DONE; step++) {
    REQUIRE(step < 12 && !reclaimstep(&work, journalsubmit, &transport) &&
                transport.ticket.txid == 50 && reclaimpublish(&work) == -ERR_WOULD_BLOCK &&
                map->direct[1] == 100 && committed->used[1] == before,
            291);
    journaldrain(&transport, journal);
  }
  REQUIRE(step == 12 && reclaimstep(&work, journalsubmit, &transport) == 1 && !work.txid &&
              !map->direct[1] && journal->last == 50 &&
              committed->used[1] == (UINT64_C(1) << (FS_DATA_START - 64)),
          293);
}

static void
testcreate(Journaltransport *transport, Cache *cache, u8 records[][FS_BLOCK_BYTES])
{
  static u8       directory[FS_BLOCK_BYTES];
  Journal        *journal = transport->journal;
  Inodemap        inodes = {{7}};
  Inodealloc      work = {0};
  Loadmap         child = {{5, 0, 0, 0}, {0}, 0}, staged;
  const Mapmeta   meta = {records[0], records[1], NULL, records[2], records[3], records[4]};
  const Dirinsert insert = {FS_ROOT, {110, directory}, NULL, NULL};
  u32             i, step;
  /* Fixture images; these bytes only distinguish the three home writes. */
  records[0][5 * FS_INODE_BYTES] = 1;
  for (i = 0; i < FS_BLOCK_BYTES; i++)
    records[1][i] = 0;
  records[1][0] = 39;
  directory[0] = 5;
  REQUIRE(!inodeallocstage(&inodes, &child, &staged) &&
              !journalinodealloc(&work, journal, cache, &inodes, &child, &staged, &meta, &insert),
          309);
  for (step = 0; journal->phase != JOURNAL_DONE; step++) {
    REQUIRE(step < 14 && !inodeallocstep(&work, journalsubmit, transport) &&
                transport->ticket.txid == 51 && !child.file.generation && inodes.used[0] == 7,
            310);
    journaldrain(transport, journal);
  }
  REQUIRE(step == 14 && inodeallocstep(&work, journalsubmit, transport) == 1 && !work.txid &&
              journal->last == 51 && child.file.generation == 1 && inodes.used[0] == 39,
          311);
}

static isize
journaluncertain(void *context, const Journalio *io, u64 offset, u32 bytes)
{
  isize error = journalsubmit(context, io, offset, bytes);
  return error ? error : JOURNAL_SUBMIT_UNCERTAIN;
}

static void
testuncertain(Journaltransport *transport, Cache *cache, u8 records[][FS_BLOCK_BYTES])
{
  Journal      *journal = transport->journal;
  Inodemap      inodes = {{39}};
  Inodealloc    work = {0};
  Loadmap       child = {{6, 0, 0, 0}, {0}, 0}, staged;
  const Mapmeta meta = {records[0], records[1], NULL, records[2], records[3], records[4]};
  Journalio    *ticket = &transport->ticket;
  Vdone         done;
  REQUIRE(!inodeallocstage(&inodes, &child, &staged) &&
              !journalinodealloc(&work, journal, cache, &inodes, &child, &staged, &meta, NULL) &&
              inodeallocstep(&work, journaluncertain, transport) == -ERR_STATE &&
              journal->phase == JOURNAL_STOPPED && journal->active && ticket->txid == 52 &&
              ticket->kind == JOURNAL_FLUSH && journal->header == meta.header &&
              inodeallocstep(&work, journaluncertain, transport) == -ERR_STATE &&
              journalrelease(journal) == -ERR_WOULD_BLOCK,
          312);
  done = waitblock(transport->queue, transport->binding, transport->event);
  REQUIRE(!blkqfinish(transport->queue, &done, NULL, 0) && !__sys_alarmcancel(&transport->alarm) &&
              !journalcomplete(journal, ticket, 1) && inodeallocpublish(&work) == -ERR_STATE &&
              !child.file.generation && inodes.used[0] == 39 && !journalrelease(journal) &&
              journal->last == 51 && journal->phase == JOURNAL_STOPPED,
          313);
  vdev_trace[3]++;
  ticket->serial = 0;
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
  Journaltransport   transport = {queue, binding, irqevent, 0, {0}, &journal};
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
    REQUIRE(!journalpump(&journal, journalsubmit, &transport) && transport.ticket.txid == 48 &&
                !map.file.length &&
                growthpublish(&work, &map, NULL, &committed, &reserved) == -ERR_WOULD_BLOCK,
            265);
    journaldrain(&transport, &journal);
    REQUIRE(++step <= 13, 266);
  }
  REQUIRE(step == 13 && !growthpublish(&work, &map, NULL, &committed, &reserved) &&
              map.file.length == FS_BLOCK_BYTES + 5 && map.direct[1] == 100 &&
              committed.used[1] == reserved.used[1] && journal.last == 48,
          267);
  testtruncate(queue, binding, irqevent, &journal, &cache, &map, &committed, &reserved, records);
  testcreate(&transport, &cache, records);
  testuncertain(&transport, &cache, records);
}

static void
resetpending(Blkqueue *queue, Handle binding, u64 irqevent, const Vblkconfig *config,
             const u64 addresses[3])
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
              blkqinit(queue, device, ring, request, queue->buffers, queue->count, addresses) ==
                  -ERR_STATE &&
              blkqsubmit(queue, 0, VBLK_READ, 0, config->blocksize, NULL, blksync, device) ==
                  -ERR_STATE,
          229);
  ringafter[0] = ringafter[1] = ringafter[2] = 0;
  failringafter = 1;
  REQUIRE(!virtioread(device->ops.context, 0x60, &causes) && !causes &&
              blkqrecover(queue, now()) == -ERR_BUSY && sync.after == 4 && !request->active &&
              !request->held && fault.after == 1 && !request[1].held &&
              queue->state == BLKQ_RECLAIM && ringafter[0] == 1 && ringafter[1] == 1 &&
              !ringafter[2],
          468);
  REQUIRE(!blkqrecover(queue, now()) && sync.after == 4 && fault.after == 1 && ringafter[0] == 1 &&
              ringafter[1] == 2 && ringafter[2] == 1 &&
              blkqinit(queue, device, ring, request, queue->buffers, queue->count, addresses) ==
                  -ERR_STALE,
          189);
  vdev_trace[16]++;
  vdev_trace[39]++;
}
static void
testresize(Blkqueue *queue, Handle binding, u64 irqevent, const Vblkconfig *old)
{
  static u8  staged[PAGE_SIZE];
  Vdev      *device = queue->device;
  Vblkconfig refreshed = {0};
  Ipcmsg     event;
  Vdone      done[VQ_BATCH], pending;
  u32        count, more, causes, i;
  u64        alarm, started = now();
  REQUIRE(blkqsubmit(queue, 0, VBLK_READ, old->sectors, old->blocksize, NULL, blksync, device) ==
              -ERR_INVALID,
          231);
  REQUIRE(started <= UINT64_MAX - VDEV_RESET_NS &&
              !__sys_alarmset(started + VDEV_RESET_NS, &alarm) && !__sys_irqack(binding),
          208);
  REQUIRE(!blkqsubmit(queue, 0, VBLK_READ, 0, old->blocksize, NULL, blksync, device), 208);
  for (;;) {
    REQUIRE(!ipc_recv(irqevent | EVENT_TIMER, RECV_EVENTS_ONLY, &event) &&
                event.kind == IPC_EVENT && event.words[0] == irqevent &&
                !blkqpoll(queue, done, &count, &more, &causes) && !more,
            209);
    REQUIRE(!__sys_irqack(binding), 226);
    if (count)
      break;
  }
  REQUIRE(count == 1, 209);
  pending = done[0];
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
  REQUIRE(blkqrefresh(queue, &refreshed) == -ERR_WOULD_BLOCK && !refreshed.sectors &&
              queue->config.sectors == old->sectors && queue->configpending &&
              !blkqfinish(queue, &pending, NULL, 0),
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
waitblock(Blkqueue *queue, Handle binding, u64 irqevent)
{
  Vdone  done[VQ_BATCH];
  Ipcmsg event;
  u32    count, more, causes;
  for (;;) {
    REQUIRE(!ipc_recv(irqevent | EVENT_TIMER, RECV_EVENTS_ONLY, &event) && event.kind == IPC_EVENT,
            241);
    REQUIRE(event.words[0] == irqevent, 351);
    REQUIRE(!blkqpoll(queue, done, &count, &more, &causes), 352);
    REQUIRE(count <= 1 && !more && !(causes & VDEV_IRQ_CONFIG), 353);
    if (count)
      break;
    /* A level IRQ can be delivered again after the prior completion drained. */
    REQUIRE(!__sys_irqack(binding), 355);
  }
  REQUIRE(causes == VDEV_IRQ_USED, 356);
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
  done = waitblock(queue, binding, irqevent);
  REQUIRE(blkqsubmit(queue, 1, VBLK_FLUSH, 0, 0, NULL, blksync, device) == -ERR_WOULD_BLOCK &&
              !blkqfinish(queue, &done, NULL, 0) && !__sys_irqack(binding) &&
              !blkqsubmit(queue, 1, VBLK_FLUSH, 0, 0, NULL, blksync, device),
          243);
  done = waitblock(queue, binding, irqevent);
  REQUIRE(blkqsubmit(queue, 0, VBLK_WRITE, sector, sizeof(source), source, blksync, device) ==
                  -ERR_WOULD_BLOCK &&
              blkqsubmit(queue, 0, VBLK_FLUSH, 0, 0, NULL, blksync, device) == -ERR_WOULD_BLOCK &&
              !blkqfinish(queue, &done, NULL, 0) && !__sys_alarmcancel(&alarm),
          244);
  vdev_trace[2]++;
  vdev_trace[3]++;
}
static const u32 blkopcodes[] = {
    BLK_WRITE, BLK_FLUSH,                                  /* data-only transaction */
    BLK_READ,                                              /* abandoned probe */
    BLK_READ,  BLK_READ,                                   /* EMPTY probe */
    BLK_READ,  BLK_READ,                                   /* COMMITTED probe */
    BLK_READ,                                              /* duplicate targets */
    BLK_READ,  BLK_READ,  BLK_READ,                        /* corrupt payload */
    BLK_READ,  BLK_READ,  BLK_READ,                        /* valid header and payload */
    BLK_READ,  BLK_READ,  BLK_READ,                        /* ownership scan */
    BLK_WRITE, BLK_WRITE, BLK_FLUSH, BLK_WRITE, BLK_FLUSH, /* replay and retire */
    BLK_READ,  BLK_READ,                                   /* re-probe home */
    BLK_READ,  BLK_READ,  BLK_READ,  BLK_READ,  BLK_READ,  /* home-only scan */
    BLK_READ,  BLK_READ,  BLK_READ,  BLK_READ,             /* root directory */
    BLK_READ,  BLK_READ,  BLK_READ,  BLK_READ,             /* directory iteration */
    BLK_READ,  BLK_READ,  BLK_READ,  BLK_READ,             /* directory lookup */
    BLK_READ,  BLK_READ,  BLK_READ,  BLK_READ,             /* symlink name */
    BLK_READ,  BLK_READ,                                   /* link inode and contents */
    BLK_READ,  BLK_READ,  BLK_READ,  BLK_READ,             /* dangling target lookup */
    BLK_READ,  BLK_READ,  BLK_READ,  BLK_READ,             /* parent name lookup */
    BLK_READ                                               /* file cache fill */
};

static isize
blkfilldone(void *job, Handle request, const Cacheio *ticket, isize status, const void *data)
{
  REQUIRE(request == 1 && (!status || status == -EAGAIN), 450);
  return status ? cachefillfinish(job, ticket, 0) : cachefillcopy(job, ticket, data, CACHE_BYTES);
}

static void
blkfillcheck(Fsblk *blk, Cache *cache)
{
  static Blkfetch work;
  Cachefill       fill = {0};
  Cacheio         ticket, old = {0};
  Cachekey        key = {0};
  Ipcmsg          msg;
  u8             *dst;
  const u8       *bytes;
  u64             version;
  u32             i;
  work.blk = blk;
  for (i = 0; i < 2; i++) {
    REQUIRE(
        cachefillstart(&fill, cache, 192, CACHE_DATA, &ticket, &dst) == 1 &&
            (!i || ticket.serial != old.serial || ticket.key.generation != old.key.generation) &&
            !blkfetchstart(&work, &fill, 1, &ticket, 192, blkfilldone) &&
            blkfetchstart(&work, &fill, 1, &ticket, 192, blkfilldone) == -ERR_WOULD_BLOCK &&
            !ipc_rqwait(blk->rpc, RQ_FINISH, &msg) && fill.cache && !blkfetchpoll(&work) &&
            !fill.cache && !blk->rpc && !work.job && work.status == (i ? 0 : -EAGAIN),
        451);
    old = ticket;
  }
  REQUIRE(!cacheget(cache, 192, CACHE_DATA, &key) && !cacheview(cache, &key, &bytes, &version),
          452);
  for (i = 0; i < CACHE_BYTES; i++)
    REQUIRE(bytes[i] == (u8) (0xd3 + i * 23), 453);
  REQUIRE(!cacheput(cache, &key), 454);
}

static isize
blksuper(const u8 *bytes, Fslayout *layout, u8 *uuid)
{
  u32 i;
  for (i = 0; i < FS_BLOCK_BYTES; i++)
    REQUIRE(!bytes[i], 394);
  *layout = (Fslayout){256, 1, 64, 65, 66, 67, 99, 1, 4096, 1024, 1};
  for (i = 0; i < 16; i++)
    uuid[i] = (u8) (i + 3);
  return 0;
}

static isize
blkmark(const u8 *bytes, const u8 *uuid, Journalmark *mark)
{
  u32 i;
  for (i = 0; i < 16; i++)
    REQUIRE(uuid[i] == i + 3, 395);
  for (i = 0; i < FS_BLOCK_BYTES; i++)
    REQUIRE(bytes[i] == (u8) (0xc1 + 4 * 7 + i * 19), 396);
  *mark = (Journalmark){47, 0, 0, 0};
  return 0;
}

static u8
blkimagebyte(u32 image, u32 offset)
{
  if (!image)
    return offset == 2 * FS_INODE_BYTES || offset == 5 * FS_INODE_BYTES;
  return offset == 0 ? 39 : 0;
}

static isize
blkcommitted(const u8 *bytes, const u8 *uuid, Journalmark *mark)
{
  REQUIRE(!blkmark(bytes, uuid, mark), 399);
  *mark = (Journalmark){48, 1, 12, 0x4807289b};
  return 0;
}

static isize
blkhead(const u8 *bytes, Journalhead *head)
{
  u32 i;
  for (i = 0; i < FS_BLOCK_BYTES; i++)
    REQUIRE(bytes[i] == (u8) (0xc1 + 2 * 7 + i * 19), 400);
  *head = (Journalhead){48, {FS_INODE_START + 1, FS_INODE_MAP}, 1, 2, 12, 0x4807289b};
  return 0;
}

static isize
blkheadduplicate(const u8 *bytes, Journalhead *head)
{
  REQUIRE(!blkhead(bytes, head), 402);
  head->targets[1] = head->targets[0];
  return 0;
}

static Blkredo *
blkclientredo(Fsblk *blk, const Blkprobe *empty)
{
  static Blkprobe probe;
  static Blkredo  work;
  Ipcmsg          result;
  u32             i, j, k;
  work.probe = empty;
  REQUIRE(blkredostep(&work) == 1 && !blk->rpc && !work.read, 403);
  probe.blk = blk;
  REQUIRE(!blkprobestep(&probe, blksuper, blkcommitted) && blk->rpc &&
              !ipc_rqwait(blk->rpc, RQ_FINISH, &result) &&
              !blkprobestep(&probe, blksuper, blkcommitted) && blk->rpc &&
              !ipc_rqwait(blk->rpc, RQ_FINISH, &result) &&
              blkprobestep(&probe, blksuper, blkcommitted) == 1 && !blk->rpc && probe.mark.state,
          404);
  for (i = 0; i < 3; i++) {
    Blkredo *redo = &work;
    redo->probe = &probe;
    redo->decode = i ? blkhead : blkheadduplicate;
    /* Neither rejected case publishes image pointers or owns a request. */
    redo->failed = redo->read = redo->crc = 0;
    redo->initial = 17;
    redo->finalxor = i == 1 ? 4 : 5;
    REQUIRE(!blkredostep(redo) && blk->rpc && !redo->images[0].bytes, 405);
    if (i == 2) {
      u64 sequence = redo->sequence;
      REQUIRE(!ipc_rqwait(blk->rpc, RQ_FINISH, &result) && !blkredostep(redo) && blk->rpc &&
                  redo->sequence > sequence && !redo->read && !redo->crc && !redo->images[0].bytes,
              447);
    }
    for (j = 0; j < (i ? 3U : 1U); j++) {
      u64   sequence = redo->sequence;
      isize status = j < 2 && i ? 0 : i == 2 ? 1 : -ERR_STATE;
      REQUIRE(sequence && sequence == blk->seq && !ipc_rqwait(blk->rpc, RQ_FINISH, &result) &&
                  blkredostep(redo) == status &&
                  (status ? !blk->rpc && !redo->sequence : blk->rpc && redo->sequence > sequence),
              406);
      if (status != 1)
        REQUIRE(!redo->images[0].bytes, 407);
    }
    REQUIRE(blkredostep(redo) == (i == 2 ? 1 : -ERR_STATE) && !blk->rpc, 408);
  }
  REQUIRE(work.images[0].block == FS_INODE_START + 1 && work.images[1].block == FS_INODE_MAP, 409);
  for (j = 0; j < 2; j++)
    for (k = 0; k < FS_BLOCK_BYTES; k++)
      REQUIRE(work.images[j].bytes[k] == blkimagebyte(j, k), 410);
  return &work;
}

static isize
blkretired(const u8 *bytes, const u8 *uuid, Journalmark *mark)
{
  u32 i;
  for (i = 0; i < 16; i++)
    REQUIRE(uuid[i] == i + 3, 417);
  for (i = 0; i < FS_BLOCK_BYTES; i++)
    REQUIRE(bytes[i] == 0x5a, 418);
  *mark = (Journalmark){48, 0, 0, 0};
  return 0;
}

static isize
blkdecodeinodes(const u8 *encoded, Inodemap *out)
{
  u32 i;
  for (i = 0; i < FS_BLOCK_BYTES; i++)
    REQUIRE(encoded[i] == blkimagebyte(1, i), 411);
  *out = (Inodemap){{(UINT64_C(1) << 34) | (UINT64_C(1) << FS_ROOT)}};
  return 0;
}

static isize
blkdecodeblocks(const u8 *encoded, u32 blocks, Blockmap *out)
{
  u32 i;
  REQUIRE(blocks == 256, 412);
  for (i = 0; i < FS_BLOCK_BYTES; i++)
    REQUIRE(encoded[i] == (i == 99 / 8 ? 1U << (99 % 8) : 0), 413);
  *out =
      (Blockmap){{0, (UINT64_C(3) << (109 - 64)) | (UINT64_C(1) << (99 - 64)), UINT64_C(1) << 63}};
  return 0;
}

static isize
blkdecodeinode(u32 ino, const u8 *encoded, Loadmap *map, u32 *kind, u32 *links)
{
  u32 i;
  /* Fixture records distinguish the home and overlay paths without a disk codec. */
  REQUIRE(ino == FS_ROOT || ino == 34, 386);
  for (i = 0; i < FS_INODE_BYTES; i++)
    REQUIRE(encoded[i] == (ino == 34 && i == 0), 387);
  map->file = (Loadfile){ino, 1, 1, FS_BLOCK_BYTES};
  map->direct[0] = ino == FS_ROOT ? 109 : 99;
  map->direct[1] = ino == FS_ROOT ? 110 : 0;
  if (ino == FS_ROOT)
    map->file.length += PAGE_SIZE;
  map->indirect = ino == FS_ROOT ? 191 : 0;
  *kind = ino == FS_ROOT ? VNODE_DIR : VNODE_FILE;
  *links = 1;
  return 0;
}

static isize
blkdecodelink(u32 ino, const u8 *encoded, Loadmap *map, u32 *kind, u32 *links)
{
  isize error = blkdecodeinode(ino, encoded, map, kind, links);
  if (!error && ino == 34) {
    *kind = VNODE_LINK;
    map->file.length = 3;
  }
  return error;
}

static isize
blkdecodeindex(u32 block, const u8 *encoded, Loadindex *index)
{
  u32 i;
  REQUIRE(block == 191, 385);
  for (i = 0; i < FS_BLOCK_BYTES; i++)
    REQUIRE(!encoded[i], 389);
  index->block = block;
  return 0;
}

static isize
blkdots(const u8 *bytes, Dirrecord *record)
{
  *record = (Dirrecord){bytes[0], 1, 24, 2, VNODE_DIR};
  return 0;
}

static isize
blknamed(const u8 *bytes, Dirrecord *record)
{
  *record = (Dirrecord){bytes[0], 1, 24, bytes[1], VNODE_FILE};
  return 0;
}

static isize
blkretire(const u8 *uuid, u64 txid, u8 *bytes)
{
  u32 i;
  REQUIRE(txid == 48 && uuid[0] == 3, 442);
  for (i = 0; i < PAGE_SIZE; i++)
    bytes[i] = 0x5a;
  return 0;
}

static isize
blkdecodedir(const u8 *bytes, Dirrecord *record)
{
  if (bytes[0] == 5) {
    *record = (Dirrecord){0, 0, PAGE_SIZE, 0, 0};
  } else if (bytes[0] == 0xb1) {
    *record = (Dirrecord){0, 0, 24, 0, 0};
  } else {
    REQUIRE(bytes[0] == (u8) (0xb1 + 24 * 37), 427);
    *record = (Dirrecord){34, 1, PAGE_SIZE - 24, 59, VNODE_FILE};
  }
  return 0;
}

typedef struct Lookupnodes Lookupnodes;
struct Lookupnodes
{
  Vmounts *mounts;
  Vnode    file;
  Vnode   *root;
};

static isize
blklinkdir(const u8 *bytes, Dirrecord *record)
{
  isize error = blkdecodedir(bytes, record);
  if (!error && record->inode == 34)
    record->kind = VNODE_LINK;
  return error;
}

static isize
blkchilddir(const u8 *bytes, Dirrecord *record)
{
  isize error = blkdecodedir(bytes, record);
  if (!error && record->inode == 34)
    record->kind = VNODE_DIR;
  return error;
}

static isize
lookuphold(void *context, Vnode *node)
{
  Lookupnodes *nodes = ((Blklookup *) context)->nodes;
  return vmounthold(nodes->mounts, node->mount);
}

static void
lookupdrop(void *context, Vnode *node)
{
  Lookupnodes *nodes = ((Blklookup *) context)->nodes;
  REQUIRE(!vmountdrop(nodes->mounts, node->mount), 476);
}

static isize
lookupmode(void *context, const Vnode *node, u32 *owner, u32 *group, u32 *mode)
{
  Lookupnodes *nodes = context;
  if (node->mount != nodes->root->mount ||
      (!vnodesame(node, nodes->root) && (node->number != 2 || node->generation != 7)))
    return -ESTALE;
  *owner = 123;
  *group = 456;
  *mode = 0710;
  return 0;
}

static isize
lookupacquire(void *context, const Vnode *identity, Vnode **out)
{
  Lookupnodes *nodes = context;
  Vnode       *node = vnodesame(identity, nodes->root) ? nodes->root : &nodes->file;
  isize        error;
  if (!vnodesame(identity, node) || identity->kind != node->kind)
    return -ESTALE;
  error = vmounthold(nodes->mounts, identity->mount);
  if (!error)
    *out = node;
  return error;
}

static isize
cwdhold(void *context, Vnode *node)
{
  Lookupnodes *nodes = ((Blkname *) context)->view->nodes;
  return vmounthold(nodes->mounts, node->mount);
}

static void
cwddrop(void *context, Vnode *node)
{
  Lookupnodes *nodes = ((Blkname *) context)->view->nodes;
  REQUIRE(!vmountdrop(nodes->mounts, node->mount), 537);
}

static u64
cwdversion(void *context)
{
  (void) context;
  return 1;
}

static isize
cwdname(void *context, const Vnode *node, Vnode **parent, const char **name, u32 *length)
{
  return blkdirname(context, node, parent, name, length);
}

static void
blkclient(const Bootview *boot, Handle call)
{
  static Fsblk      blk;
  static Blkprobe   probe;
  static Blkprobe   homeprobe;
  static Blkread    reader;
  static Cacheblock storage;
  static Txn        txn;
  static Cacheblock syncstorage[2];
  static Blkfetch   fetch;
  static Journal    recovered;
  static Blkowners  owners;
  static Blkowners  home;
  Dirnode           nodes[FS_INODES] = {{0}};
  Blkdir            dirs = {0};
  Blkmount          mounting = {0};
  Blkmount          abandoned = {0};
  Vmounts           mounts = {0};
  Vnode             root = {0};
  u8                namebytes[256];
  u32               nameoffsets[8];
  static u8         data[PAGE_SIZE];
  static u8         snapshot[PAGE_SIZE];
  const Loadmap     file = {{2, 1, 1, PAGE_SIZE}, {192}, 0};
  const Handle      request = ((u64) HANDLE_REQUEST << 59) | (1U << 16);
  Syncwork          sync = {&txn, snapshot, 1, 0};
  Blklog            log = {&blk, &txn.journal, {0}, 0, 0};
  Cache             dirty = {0};
  Cachekey          key = {0};
  Cachefill         waiting = {0};
  Cacheio           ticket;
  u8               *dst;
  const u64         address = 0x2300000;
  Handle            space = bootresource(boot, BOOT_SELF_SPACE);
  Msg               message = {{BLK_WORD(BLK_HELLO), 0, 1, UINT64_MAX}};
  Ipcmsg            result;
  Blkattach         attaching = {0};
  Cache             cache = {0};
  u32               i, op;
  Msg               badhello = {{BLK_WORD(BLK_HELLO), 0, 1, 0}};
  {
    isize status = 1;
    REQUIRE(!fsblkhello(&attaching, call, space, address, 1, 1) &&
                fsblkhello(&attaching, call, space, address, 1, 1) == -ERR_INVALID &&
                !ipc_rqwait(attaching.boot.rpc, RQ_FINISH, &result) &&
                !sysbootpoll(&attaching.boot, attaching.boot.claim),
            360);
    if (attaching.boot.kind == IPC_RESOURCE_REPLY) {
      Allocarg reserve = {PAGE_SIZE, ALLOC_DATA, 0, 0, 0, 0};
      Maparg   overlap = {0, 0, address, PAGE_SIZE, MAP_READ | MAP_WRITE, 0};
      REQUIRE(!__sys_alloc(bootresource(boot, BOOT_SELF_ALLOC), &reserve, &overlap.mem) &&
                  !__sys_map(space, &overlap) &&
                  fsblkattached(&attaching, &blk, &status) == -ERR_STATE && status == 1 &&
                  !blk.address && !attaching.boot.rpc && attaching.boot.resource &&
                  !attaching.map.mapped && !__sys_unmap(space, address) && !__sys_free(overlap.mem),
              363);
    }
    REQUIRE(!fsblkattached(&attaching, &blk, &status), 360);
    if (status) {
      REQUIRE(status == -ERR_NOTSUP && !blk.address && !fsblkdetach(&attaching, &blk), 361);
      return;
    }
    REQUIRE(blk.address == address && attaching.memory && !attaching.boot.rpc &&
                attaching.map.verified && blk.seq == 1,
            362);
  }
  REQUIRE(!ipc_call(call, &badhello, 0, &result) && result.kind == IPC_NONE &&
              (isize) result.words[0] == -EINVAL && result.words[1] == 1 && !result.words[2] &&
              !result.words[3],
          456);
  probe.blk = &blk;
  abandoned.probe = &probe;
  abandoned.dirs = &dirs;
  abandoned.storage = &storage;
  abandoned.layout = blksuper;
  abandoned.mark = blkmark;
  dirs.home = &home;
  REQUIRE(!blkmountstep(&abandoned) && blk.rpc && blk.opcode == BLK_INFO && !probe.layout.blocks &&
              !blk.info.geometry_generation && !ipc_rqwait(blk.rpc, RQ_FINISH, &result) &&
              !blkmountstep(&abandoned) && !blk.rpc && !probe.layout.blocks && blk.seq == 2 &&
              !blk.info.geometry_generation && !abandoned.failed && !blkmountstep(&abandoned) &&
              blk.rpc && blk.opcode == BLK_INFO && blk.seq == 3 &&
              !ipc_rqwait(blk.rpc, RQ_FINISH, &result) && !blkmountstep(&abandoned) && !blk.rpc &&
              !probe.layout.blocks,
          364);
  REQUIRE(blk.info.geometry_generation == 2 && blk.info.capacity_bytes == 2048 * 512 &&
              blk.info.logical_bytes == PAGE_SIZE && blk.info.flags == BLK_FLUSHABLE,
          365);
  for (i = 0; i < PAGE_SIZE; i++)
    data[i] = (u8) (0xd3 + i * 23);
  fetch.blk = &blk;
  fetch.journal = &txn.journal;
  REQUIRE(!cacheinit(&dirty, syncstorage, ARRAY_SIZE(syncstorage),
                     blk.info.capacity_bytes / PAGE_SIZE) &&
              !txninit(&txn, &dirty, 47) && !cacheget(&dirty, 192, CACHE_DATA, &key) &&
              !cachereadbegin(&dirty, &key, &ticket, &dst) && !cachecomplete(&dirty, &ticket, 1) &&
              !cacheedit(&dirty, &key, 0, data, PAGE_SIZE),
          378);
  REQUIRE(!filesyncstep(&sync, request, RQ_CHECK_LIVE, &file, NULL, blklogsync, &log) &&
              log.ticket.serial == 1 && !ipc_rqwait(blk.rpc, RQ_FINISH, &result) &&
              !blklogpoll(&log) && !log.ticket.serial && !blk.rpc &&
              txn.journal.phase == JOURNAL_DATA_WRITE && !txn.journal.active &&
              !txn.journal.durable && sync.request == request,
          381);
  REQUIRE(cachefillstart(&waiting, &dirty, 193, CACHE_DATA, &ticket, &dst) == 1 &&
              blkfetchstart(&fetch, &waiting, 1, &ticket, 193, blkfilldone) == -ERR_WOULD_BLOCK &&
              !blk.rpc && !cachefillfinish(&waiting, &ticket, 0),
          455);
  for (op = 0; op < 2; op++)
    REQUIRE(!filesyncstep(&sync, request, RQ_CHECK_LIVE, &file, NULL, blklogsync, &log) &&
                log.ticket.serial == op + 2 && !ipc_rqwait(blk.rpc, RQ_FINISH, &result) &&
                !filesyncstep(&sync, request, RQ_CHECK_LIVE, &file, NULL, blklogsync, &log) &&
                sync.request == request && !txn.journal.durable && !blklogpoll(&log) &&
                !log.ticket.serial && !blk.rpc,
            379);
  REQUIRE(txn.journal.durable &&
              filesyncstep(&sync, request, RQ_CHECK_LIVE, &file, NULL, blklogsync, &log) == 1 &&
              !sync.request && !syncstorage[key.index].dirty && txn.journal.last == 47 &&
              !cacheput(&dirty, &key),
          380);
  REQUIRE(!blkmountstep(&abandoned) && blk.rpc && !ipc_rqwait(blk.rpc, RQ_FINISH, &result), 443);
  {
    u64 seq = blk.seq;
    REQUIRE(blkmountstop(&abandoned, -ERR_TIMEDOUT) == -ERR_TIMEDOUT && !blk.rpc &&
                !probe.layout.blocks && blkmountstep(&abandoned) == -ERR_TIMEDOUT && blk.seq == seq,
            444);
  }
  probe = (Blkprobe){0};
  probe.blk = &blk;
  dirs.failed = 0;
  REQUIRE(blkreadboot(&blk, JOURNAL_MARK_BLOCK + 1) == -ERR_INVALID && !blk.rpc &&
              !blkprobestep(&probe, blksuper, blkmark) && blk.rpc && !probe.layout.blocks,
          393);
  REQUIRE(!ipc_rqwait(blk.rpc, RQ_FINISH, &result) && !blkprobestep(&probe, blksuper, blkmark) &&
              blk.rpc && probe.layout.blocks == 256 && !probe.mark.txid,
          397);
  REQUIRE(!ipc_rqwait(blk.rpc, RQ_FINISH, &result) &&
              blkprobestep(&probe, blksuper, blkmark) == 1 && !blk.rpc && probe.mark.txid == 47 &&
              !probe.mark.state && !probe.geometry.faultbytes &&
              blkprobestep(&probe, blksuper, blkmark) == 1 && !blk.rpc,
          398);
  mounting.probe = &probe;
  mounting.dirs = &dirs;
  mounting.storage = &storage;
  mounting.layout = blksuper;
  mounting.mark = blkmark;
  mounting.log.blk = &blk;
  mounting.log.journal = &recovered;
  mounting.writable = 1;
  REQUIRE(blkmountstep(&mounting) == -ERR_NOTSUP && !blk.rpc && !recovered.blocks && !home.home &&
              blkmountadd(&mounting, &mounts, &root, NULL) == -ERR_NOTSUP && !mounts.serial,
          445);
  mounting = (Blkmount){0};
  owners.redo = blkclientredo(&blk, &probe);
  reader.blk = &blk;
  owners.load.read = &reader;
  owners.load.cache = &cache;
  owners.load.inode = blkdecodeinode;
  owners.load.indirect = blkdecodeindex;
  owners.inodemap = blkdecodeinodes;
  owners.blockmap = blkdecodeblocks;
  REQUIRE(!cacheinit(&cache, &storage, 1, 256) && !blkownersstep(&owners) && !blk.rpc &&
              owners.inodes.used[0] && !blkownersstep(&owners) && blk.rpc && !owners.scan.next &&
              blkredoadmit(&owners, &recovered, FS_BLOCK_BYTES, 0, data) == -ERR_WOULD_BLOCK &&
              !ipc_rqwait(blk.rpc, RQ_FINISH, &result) && !blkownersstep(&owners) && !blk.rpc &&
              owners.scan.next == FS_ROOT,
          384);
  for (i = 0; i < 2; i++)
    REQUIRE(!blkownersstep(&owners) && blk.rpc && owners.scan.next == FS_ROOT &&
                !owners.owners.owned.used[1] && !ipc_rqwait(blk.rpc, RQ_FINISH, &result),
            390);
  for (i = 0; i < FS_INODES / 32; i++)
    REQUIRE(blkownersstep(&owners) == (i == FS_INODES / 32 - 1) &&
                owners.scan.next == (i + 1) * 32 && !blk.rpc,
            367);
  REQUIRE(owners.owners.owned.used[1] == owners.allocation.used[1] &&
              owners.owners.metadata.used[1] == (UINT64_C(3) << (109 - 64)) &&
              owners.owners.owned.used[2] == owners.allocation.used[2] &&
              owners.owners.metadata.used[2] == owners.allocation.used[2],
          388);
  REQUIRE(blkredoadmit(&owners, &recovered, 0, 0, data) < 0 && !blk.rpc, 414);
  home.load.read = &reader;
  home.load.cache = &cache;
  home.load.inode = blkdecodeinode;
  home.load.indirect = blkdecodeindex;
  home.load.nodes = nodes;
  home.inodemap = blkdecodeinodes;
  home.blockmap = blkdecodeblocks;
  dirs.home = &home;
  dirs.decode = blkdecodedir;
  dirs.names =
      (Dirnames){namebytes, nameoffsets, sizeof(namebytes), ARRAY_SIZE(nameoffsets), 0, 0, 0, 0};
  dirs.graph.nodes = nodes;
  data[0] = 0xb1;
  REQUIRE(dirgraphblock(&dirs.graph, &dirs.names, FS_ROOT, data, 16, blkdecodedir) == -ERR_STATE &&
              !dirs.graph.incoming[34],
          431);
  dirs.graph.failed = 0;
  mounting.probe = (Blkprobe *) owners.redo->probe;
  mounting.after = &homeprobe;
  mounting.overlay = &owners;
  mounting.dirs = &dirs;
  mounting.log.blk = &blk;
  mounting.log.journal = &recovered;
  mounting.storage = &storage;
  mounting.capacity = 1;
  mounting.empty = data;
  mounting.layout = blksuper;
  mounting.mark = blkretired;
  mounting.retire = blkretire;
  mounting.faultbytes = FS_BLOCK_BYTES;
  mounting.writable = 1;
  op = 0;
  for (i = 0; i < 3 * FS_INODES / 32 + 40; i++) {
    isize done = blkmountstep(&mounting);
    REQUIRE(done == 0 || done == 1, 425);
    if (done)
      break;
    REQUIRE(blkmountadd(&mounting, &mounts, &root, NULL) == -ERR_WOULD_BLOCK && !mounts.serial,
            438);
    REQUIRE(blkmountroot(&mounting, &root) == -ERR_WOULD_BLOCK && !root.number, 438);
    if (blk.rpc) {
      REQUIRE(!ipc_rqwait(blk.rpc, RQ_FINISH, &result), 426);
      op++;
    }
  }
  REQUIRE(op == 19 && dirs.graph.incoming[34] == 1 && blkmountstep(&mounting) == 1 && !blk.rpc &&
              recovered.last == 48 && recovered.phase == JOURNAL_IDLE && !home.scan.count &&
              home.owners.owned.used[1] == owners.owners.owned.used[1],
          429);
  REQUIRE(blkmountadd(&(Blkmount){.probe = mounting.probe, .dirs = &dirs}, &mounts, &root, NULL) ==
                  -ERR_WOULD_BLOCK &&
              !mounts.serial && !root.mount,
          446);
  blk.stale = 1;
  REQUIRE(blkmountadd(&mounting, &mounts, &root, NULL) == -ERR_STALE && !mounts.serial &&
              !root.mount && blkmountroot(&mounting, &root) == -ERR_STALE && !root.number,
          441);
  blk.stale = 0;
  REQUIRE(!blkmountroot(&mounting, &root) && root.number == FS_ROOT &&
              root.generation == nodes[FS_ROOT].generation && root.kind == VNODE_DIR &&
              !root.mount && !mounts.serial,
          439);
  root.generation++;
  REQUIRE(blkmountadd(&mounting, &mounts, &root, NULL) == -ERR_STALE && !mounts.serial &&
              !root.mount,
          439);
  root.generation--;
  REQUIRE(!blkmountadd(&mounting, &mounts, &root, NULL) && root.mount == 1 && mounts.serial == 1 &&
              mounts.entries[0].root == &root &&
              blkmountadd(&mounting, &mounts, &root, NULL) == -ERR_STATE,
          440);
  {
    Blkiter   iter = {.home = &home,
                      .decode = blkdecodedir,
                      .inode = (u32) root.number,
                      .generation = (u32) root.generation};
    Blkiter   cancelled = iter;
    Blkiter   contender = iter;
    Blklookup competing = {
        .home = &home, .decode = blkdecodedir, .mount = root.mount, .acquire = lookupacquire};
    Dirrecord item = {0};
    const u8 *name = NULL;
    Vnode    *other = NULL;
    u64       linkbytes = 0;
    isize     step = 0;
    u32       waits = 1;
    REQUIRE(!cacheinit(&cache, &storage, 1, 256) && !blkiterstep(&cancelled, &item, &name) &&
                blk.rpc,
            523);
    REQUIRE(blkiterstop(&cancelled) == -ERR_WOULD_BLOCK && blk.rpc && !cancelled.offset &&
                blkiterstep(&cancelled, &item, &name) == -ERR_CANCELLED &&
                blkiterstep(&iter, &item, &name) == -ERR_BUSY && !item.inode && !name,
            532);
    REQUIRE(!ipc_rqwait(blk.rpc, RQ_FINISH, &result) && !blkiterstop(&cancelled) && !blk.rpc &&
                !cancelled.offset && !item.inode && !name,
            533);
    for (i = 0; i < 24 && !step; i++) {
      u64 before = iter.offset;
      step = blkiterstep(&iter, &item, &name);
      if (!step && blk.rpc) {
        Handle pending = blk.rpc;
        REQUIRE(iter.offset == before && !item.inode && !name, 524);
        REQUIRE(!blkiterstop(&cancelled) && blk.rpc == pending, 534);
        REQUIRE(!ipc_rqwait(blk.rpc, RQ_FINISH, &result), 525);
        waits++;
      }
    }
    REQUIRE(step == 1 && !blk.rpc, 526);
    REQUIRE(waits == 3, 528);
    REQUIRE(iter.offset == FS_BLOCK_BYTES, 529);
    REQUIRE(item.inode == 34 && item.namelen == 59 && name && name[0] == (u8) (0xb1 + 40 * 37) &&
                !name[item.namelen],
            530);
    REQUIRE(blkiterstep(&contender, &item, &name) == -ERR_BUSY &&
                blklookup(&competing, &root, "other", 5, &other) == -EBUSY && !other &&
                blkreadlink(&competing, &(Vnode){root.mount, 34, 1, VNODE_LINK},
                            &(const char *){NULL}, &linkbytes) == -EBUSY &&
                !linkbytes && !blk.rpc && item.inode == 34,
            535);
    for (i = 0, step = 0; i < 8 && !step; i++) {
      u64 before = iter.offset;
      step = blkiterstep(&iter, &item, &name);
      if (!step && blk.rpc) {
        REQUIRE(iter.offset == before && item.inode == 34 &&
                    !ipc_rqwait(blk.rpc, RQ_FINISH, &result),
                531);
        waits++;
      }
    }
    REQUIRE(step == 2 && waits == 4 && !blk.rpc && iter.offset == 2 * FS_BLOCK_BYTES &&
                item.inode == 34 && blkiterstep(&iter, &item, &name) == 2 && !blkiterstop(&iter),
            527);
    contender.generation++;
    REQUIRE(blkiterstep(&contender, &item, &name) == -ERR_STALE && !blk.rpc &&
                !blkiterstop(&contender),
            536);
  }
  {
    static Vwalk   walk;
    char           name[60];
    Lookupnodes    targets = {&mounts, {root.mount, 34, 1, VNODE_FILE}, &root};
    Vcred          cred = {124, 457};
    Blklookup      lookup = {.home = &home,
                             .graph = &dirs.graph,
                             .decode = blkdecodedir,
                             .mount = root.mount,
                             .nodes = &targets,
                             .cred = &cred,
                             .mode = lookupmode,
                             .acquire = lookupacquire};
    const Vwalkops ops = {lookuphold, lookupdrop, blksearch, blklookup, blkparent, blkreadlink};
    Vnode         *found = NULL;
    isize          step = VWALK_MORE;
    for (i = 0; i + 1 < sizeof(name); i++)
      name[i] = (char) (u8) (0xb1 + (40 + i) * 37);
    name[i] = 0;
    REQUIRE(!cacheinit(&cache, &storage, 1, 256) &&
                !vwalkinit(&walk, &mounts, &ops, &lookup, &root, &root, name, sizeof(name), 1),
            480);
    for (i = 0; i < 4 && step == VWALK_MORE; i++) {
      step = vwalkstep(&walk, &found);
      REQUIRE(!found && !lookup.find.inode && !blk.rpc, 481);
    }
    REQUIRE(step == -EACCES && !mounts.entries[0].uses, 482);
    cred.gid = 456;
    step = VWALK_MORE;
    REQUIRE(!vwalkinit(&walk, &mounts, &ops, &lookup, &root, &root, name, sizeof(name), 1), 473);
    for (i = 0; i < 24 && (step == VWALK_MORE || step == VWALK_WAIT); i++) {
      REQUIRE(!found, 473);
      step = vwalkstep(&walk, &found);
      if (step == VWALK_WAIT)
        REQUIRE(blk.rpc && !ipc_rqwait(blk.rpc, RQ_FINISH, &result), 474);
    }
    REQUIRE(!step && found == &targets.file && !lookup.find.inode && !blk.rpc &&
                mounts.entries[0].uses == 1,
            475);
    lookupdrop(&lookup, found);
    {
      Vnode child = {root.mount, 2, 7, VNODE_DIR};
      found = NULL;
      dirs.graph.next = 0;
      nodes[2] = (Dirnode){7, VNODE_DIR, 1};
      REQUIRE(!dirgraphedge(&dirs.graph, FS_ROOT, 2, 7, VNODE_DIR) &&
                  blkparent(&lookup, &child, &found) == -EIO && !found,
              477);
      for (i = 0; i < FS_INODES / 32; i++)
        REQUIRE(dirgraphstep(&dirs.graph) == (i == FS_INODES / 32 - 1), 477);
      child.generation++;
      REQUIRE(blkparent(&lookup, &child, &found) == -ESTALE && !found && !mounts.entries[0].uses,
              478);
      child.generation--;
      REQUIRE(!vwalkinit(&walk, &mounts, &ops, &lookup, &root, &child, "../..", 6, 1), 479);
      for (i = 0, step = VWALK_MORE; i < 12 && step == VWALK_MORE; i++)
        step = vwalkstep(&walk, &found);
      REQUIRE(!step && found == &root && !blk.rpc && mounts.entries[0].uses == 1, 479);
      lookupdrop(&lookup, found);
      nodes[2] = (Dirnode){0};
      dirs.graph.parent[2] = dirs.graph.incoming[2] = dirs.graph.reach[2] = 0;
    }
    {
      Vnode       link = {root.mount, 34, 2, VNODE_LINK};
      const char *target = NULL;
      u64         bytes = 0;
      nodes[34].kind = VNODE_LINK;
      home.load.inode = blkdecodelink;
      REQUIRE(!lookuphold(&lookup, &link) &&
                  ops.readlink(&lookup, &link, &target, &bytes) == -ESTALE && !target && !bytes &&
                  !blk.rpc && !lookup.link.number,
              483);
      lookupdrop(&lookup, &link);
      targets.file.kind = VNODE_LINK;
      lookup.decode = blklinkdir;
      found = NULL;
      REQUIRE(!vwalkinit(&walk, &mounts, &ops, &lookup, &root, &root, name, sizeof(name), 1), 484);
      for (i = 0, step = VWALK_MORE; i < 40 && (step == VWALK_MORE || step == VWALK_WAIT); i++) {
        step = vwalkstep(&walk, &found);
        if (step == VWALK_WAIT)
          REQUIRE(!found && blk.rpc && !ipc_rqwait(blk.rpc, RQ_FINISH, &result), 484);
      }
      REQUIRE(step == -ENOENT && !found && !blk.rpc && !lookup.link.number &&
                  !mounts.entries[0].uses && !lookup.find.inode && walk.path.links == 1 &&
                  walk.path.bytes == 4 && (u8) walk.path.text[0] == 0xb4 &&
                  (u8) walk.path.text[1] == 0xd9 && (u8) walk.path.text[2] == 0xfe &&
                  !walk.path.text[3],
              485);
      home.load.inode = blkdecodeinode;
      nodes[34].kind = VNODE_FILE;
    }
    {
      static Fscwd   cwd;
      Blkname        reverse = {.view = &lookup};
      const Fscwdops ops = {cwdhold, cwddrop, cwdversion, cwdname};
      Fscontext      context = {.root = &root, .cwd = &targets.file};
      char           path[61] = {0};
      u32            bytes = 0, waits = 0;
      nodes[34].kind = targets.file.kind = VNODE_DIR;
      dirs.graph.parent[34] = FS_ROOT;
      lookup.decode = blkchilddir;
      REQUIRE(!cacheinit(&cache, &storage, 1, 256) &&
                  !fscwdinit(&cwd, &mounts, &ops, &reverse, &context),
              538);
      for (i = 0, step = VWALK_MORE; i < 24 && (step == VWALK_MORE || step == VWALK_WAIT); i++) {
        REQUIRE(!bytes && !path[0], 539);
        step = fscwdstep(&cwd, path, sizeof(path), &bytes);
        if (step == VWALK_WAIT) {
          REQUIRE(blk.rpc && !ipc_rqwait(blk.rpc, RQ_FINISH, &result), 540);
          waits++;
        }
      }
      REQUIRE(!step, 541);
      REQUIRE(waits == 4, 542);
      REQUIRE(bytes == sizeof(path) && path[0] == '/' && !memcmp(path + 1, name, sizeof(name)),
              543);
      REQUIRE(!mounts.entries[0].uses && !reverse.child.number && !home.load.reader && !blk.rpc,
              544);
      nodes[34].kind = VNODE_FILE;
      dirs.graph.parent[34] = 0;
    }
  }
  REQUIRE(dirgraphedge(&dirs.graph, FS_ROOT, 34, 1, VNODE_FILE) == -ERR_STATE, 430);
  dirs.graph.next = 0;
  nodes[35] = nodes[36] = (Dirnode){1, VNODE_FILE, 1};
  data[0] = 35;
  data[1] = 1;
  data[16] = 'a';
  data[17] = 0;
  {
    Dirrecord found = {0};
    const u8 *name = NULL;
    u32       offset = 0;
    REQUIRE(dirblockfind(nodes, data, 24, blknamed, "a", 1, &found) == 1 && found.inode == 35 &&
                found.generation == 1 && found.kind == VNODE_FILE &&
                !dirblockfind(nodes, data, 24, blknamed, "ab", 2, &found) && found.inode == 35,
            470);
    for (i = 24; i < 48; i++)
      data[i] = 0;
    data[24] = 36;
    data[25] = 1;
    data[40] = 'a';
    found.inode = 0;
    REQUIRE(dirblockfind(nodes, data, 48, blknamed, "a", 1, &found) == -ERR_STATE && !found.inode &&
                dirblockfind(nodes, data, 25, blknamed, "a", 1, &found) == -ERR_STATE &&
                !found.inode,
            471);
    nodes[35].generation++;
    REQUIRE(dirblockfind(nodes, data, 24, blknamed, "a", 1, &found) == -ERR_STATE && !found.inode,
            472);
    nodes[35].generation--;
    data[24] = 0;
    for (i = 48; i < 72; i++)
      data[i] = 0;
    data[48] = 36;
    data[49] = 1;
    data[64] = 'b';
    REQUIRE(dirblocknext(nodes, data, 72, blknamed, &offset, &found, &name) == 1 && offset == 24 &&
                found.inode == 35 && name == data + 16,
            520);
    nodes[36].generation++;
    REQUIRE(dirblocknext(nodes, data, 72, blknamed, &offset, &found, &name) == -ERR_STATE &&
                offset == 24 && found.inode == 35 && name == data + 16,
            521);
    nodes[36].generation--;
    REQUIRE(dirblocknext(nodes, data, 72, blknamed, &offset, &found, &name) == 1 && offset == 72 &&
                found.inode == 36 && name == data + 64 && *name == 'b' &&
                !dirblocknext(nodes, data, 72, blknamed, &offset, &found, &name) && offset == 72 &&
                found.inode == 36 && name == data + 64,
            522);
  }
  REQUIRE(!dirgraphblock(&dirs.graph, &dirs.names, FS_ROOT, data, 24, blknamed), 435);
  data[0] = 36;
  REQUIRE(!dirgraphblock(&dirs.graph, &dirs.names, FS_ROOT, data, 24, blknamed) &&
              dirnamesstep(&dirs.names) == -ERR_STATE,
          436);
  dirs.names.bytes[dirs.names.offsets[1]] = 'b';
  REQUIRE(dirnamesstep(&dirs.names) == 1, 437);
  nodes[2] = nodes[3] = (Dirnode){1, VNODE_DIR, 1};
  data[0] = 3;
  data[16] = data[17] = '.';
  data[18] = 0;
  REQUIRE(!dirgraphblock(&dirs.graph, &dirs.names, 2, data, 24, blkdots) &&
              dirgraphblock(&dirs.graph, &dirs.names, 2, data, 24, blkdots) == -ERR_STATE,
          433);
  dirs.graph.failed = 0;
  REQUIRE(!dirgraphedge(&dirs.graph, FS_ROOT, 2, 1, VNODE_DIR) &&
              !dirgraphedge(&dirs.graph, FS_ROOT, 3, 1, VNODE_DIR) &&
              dirgraphstep(&dirs.graph) == -ERR_STATE,
          434);
  dirs.graph.failed = dirs.graph.next = 0;
  dirs.graph.parent[2] = dirs.graph.parent[3] = 0;
  dirs.graph.incoming[2] = dirs.graph.incoming[3] = 0;
  dirs.graph.dotdot[2] = 0;
  REQUIRE(!dirgraphedge(&dirs.graph, 2, 3, 1, VNODE_DIR) &&
              !dirgraphedge(&dirs.graph, 3, 2, 1, VNODE_DIR) &&
              dirgraphstep(&dirs.graph) == -ERR_STATE,
          432);
  blkfillcheck(&blk, &cache);
  REQUIRE(!fsblksubmit(&blk, BLK_READ, 192 * PAGE_SIZE, PAGE_SIZE, NULL) &&
              !ipc_rqwait(blk.rpc, RQ_FINISH, &result),
          460);
  {
    isize status;
    data[0] = 0x61;
    REQUIRE(!fsblkpoll(&blk, data, PAGE_SIZE, &status) && status == -EIO && data[0] == 0x61, 461);
  }
  REQUIRE(!ipc_call(call, &message, CALL_RECV_MEM, &result) && result.kind == IPC_NONE &&
              (isize) result.words[0] == -EIO && result.words[1] == message.words[2],
          462);
  REQUIRE(!fsblkdetach(&attaching, &blk) && !blk.address && !attaching.memory, 368);
  vdev_trace[VTEST_BLK_RPC] = 1;
}

static void
blkrpc(Blkqueue *queue, Handle binding, u64 irqevent)
{
  const Bootview *boot = sysboot();
  Filelink        link = {0};
  Blkjob          jobs[2];
  Blktransport    transport = {0};
  Blkservice      service = {&transport,
                             bootresource(boot, VTEST_OBSERVE),
                             bootresource(boot, BOOT_SELF_ALLOC),
                             bootresource(boot, BOOT_SELF_SPACE),
                             binding,
                             0x2300000,
                             irqevent,
                             0,
                             0};
  Ipcmsg          input;
  Vdone           done[VQ_BATCH];
  u32             count, more, causes, i;
  u64             alarm, started;
  REQUIRE(!blktransportinit(&transport, queue, &link, 1, jobs, ARRAY_SIZE(jobs), 1, blksync,
                            queue->device),
          369);
  REQUIRE(!blkservestep(&service) && link.ready && !blkservestep(&service) && link.ready &&
              link.generation == 1,
          370);
  REQUIRE(!ipc_recv(0, 0, &input) && input.words[0] == BLK_WORD(BLK_INFO) &&
              !ipc_reply(input.request, &(Msg){{(u64) -EAGAIN, input.words[2], 0, 0}}),
          371);
  REQUIRE(!blkservestep(&service), 371);
  REQUIRE(!ipc_recv(0, 0, &input) && input.words[0] == BLK_WORD(BLK_WRITE), 382);
  /* Hold admission at the same geometry for the first WRITE only. */
  queue->configpending = 1;
  REQUIRE(!blktransportcall(&transport, &input) && !transport.active, 383);
  queue->configpending = 0;
  for (i = 0; i < ARRAY_SIZE(blkopcodes); i++) {
    REQUIRE(!ipc_recv(0, 0, &input) && input.words[0] == BLK_WORD(blkopcodes[i]), 372);
    /* Exercise redo, re-probe, home bitmap, directory and link READ retries. */
    if (i == 11 || i == 22 || i == 24 || i + 24 == ARRAY_SIZE(blkopcodes) ||
        i + 12 == ARRAY_SIZE(blkopcodes) || i + 10 == ARRAY_SIZE(blkopcodes) ||
        i + 8 == ARRAY_SIZE(blkopcodes) || i + 6 == ARRAY_SIZE(blkopcodes) ||
        i + 1 == ARRAY_SIZE(blkopcodes)) {
      queue->configpending = 1;
      REQUIRE(!blktransportcall(&transport, &input) && !transport.active, 448);
      queue->configpending = 0;
      REQUIRE(!ipc_recv(0, 0, &input) && input.words[0] == BLK_WORD(BLK_READ), 449);
    }
    started = now();
    REQUIRE(started <= UINT64_MAX - VDEV_RESET_NS &&
                !__sys_alarmset(started + VDEV_RESET_NS, &alarm) && !__sys_irqack(binding) &&
                !blkservicecall(&service, &input) && transport.active == 1,
            372);
    if (i + 1 == ARRAY_SIZE(blkopcodes)) {
      while (transport.active)
        REQUIRE(!blkservestep(&service), 457);
      REQUIRE(!service.pending && !__sys_alarmcancel(&alarm), 458);
      vdev_trace[38]++;
      break;
    }
    do {
      REQUIRE(!ipc_recv(irqevent | EVENT_TIMER, RECV_EVENTS_ONLY, &input) &&
                  input.kind == IPC_EVENT && input.words[0] == irqevent &&
                  !blktransportpoll(&transport, done, &count, &more, &causes) && !more &&
                  !(causes & VDEV_IRQ_CONFIG),
              373);
      if (!count)
        REQUIRE(!__sys_irqack(binding), 374);
    } while (!count);
    REQUIRE(count == 1 && causes == VDEV_IRQ_USED && !transport.active &&
                !__sys_alarmcancel(&alarm),
            375);
    vdev_trace[38]++;
  }
  {
    Resetsync fault = {queue->device, 0, 1};
    isize     error;
    u64       state;
    transport.sync = resetsync;
    transport.syncctx = &fault;
    REQUIRE(!ipc_recv(0, 0, &input) && input.words[0] == BLK_WORD(BLK_READ) &&
                blkservicecall(&service, &input) == -ERR_FAULT && transport.active == 1 &&
                !blktransportonline(&transport) && queue->slots[0].held == 1 && !fault.after &&
                !ipc_rqcheck(input.request, &state) && state == RQ_CHECK_LIVE,
            463);
    do {
      error = blkservestop(&service, now());
    } while (error == -ERR_WOULD_BLOCK);
    REQUIRE(!error && fault.after == 1 && !queue->slots[0].held && !transport.active &&
                !service.pending && !blktransportonline(&transport),
            464);
    REQUIRE(!blkservestep(&service) && link.generation == 1 && link.ready, 465);
  }
  while (link.ready)
    REQUIRE(!blkservestep(&service), 376);
  REQUIRE(!link.address && !link.memory && !service.retiring, 459);
  vdev_trace[2] += 4;
  vdev_trace[3] += 3;
  vdev_trace[VTEST_BLK_RPC] = 1;
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
  u64        ringaddr[3];
  u32        count, more, i;
  Handle     binding = bootresource(sysboot(), VTEST_IRQ_TAG + member);
  u64        irqevent = UINT64_C(1) << (IRQ_BIT_MIN + member);
  u64        stride = config->blocksize / VBLK_SECTOR;
  Vringops   ops = {device, coherentsync, virtiofence, notify};
  void      *mapping[3];

  REQUIRE(binding > 0, 182);
  dma.va = VTEST_DMA_BASE + member * VTEST_PAGES * PAGE_SIZE;
  dma.addresses = &vdev_trace[VTEST_DMA_TRACE + member * VTEST_PAGES];
  for (i = 0; i < ARRAY_SIZE(ringaddr); i++) {
    ringaddr[i] = dma.addresses[i];
    mapping[i] = (void *) (dma.va + i * PAGE_SIZE);
  }
  testbuffer(&dma, 3, &buffers[0]);
  testbuffer(&dma, 6, &buffers[1]);
  buffers[1].statusaddr = buffers[0].dataaddr + PAGE_SIZE - 1;
  REQUIRE(blkqstart(&queue, device, &ring, 1, mapping, ringaddr, &ops, requests, buffers,
                    ARRAY_SIZE(requests)) == -ERR_INVALID &&
              !queue.state,
          224);
  testbuffer(&dma, 6, &buffers[1]);
  buffers[1].status = buffers[0].data;
  REQUIRE(blkqstart(&queue, device, &ring, 1, mapping, ringaddr, &ops, requests, buffers,
                    ARRAY_SIZE(requests)) == -ERR_INVALID &&
              !queue.state,
          224);
  testbuffer(&dma, 6, &buffers[1]);
  buffers[1].statusaddr = dma.addresses[2] + PAGE_SIZE - 1;
  REQUIRE(blkqstart(&queue, device, &ring, 1, mapping, ringaddr, &ops, requests, buffers,
                    ARRAY_SIZE(requests)) == -ERR_INVALID &&
              !queue.state,
          224);
  testbuffer(&dma, 6, &buffers[1]);
  buffers[1].status = mapping[1];
  REQUIRE(blkqstart(&queue, device, &ring, 1, mapping, ringaddr, &ops, requests, buffers,
                    ARRAY_SIZE(requests)) == -ERR_INVALID &&
              !queue.state,
          224);
  testbuffer(&dma, 6, &buffers[1]);
  REQUIRE(!blkqstart(&queue, device, &ring, 1, mapping, ringaddr, &ops, requests, buffers,
                     ARRAY_SIZE(requests)),
          224);
  transfer(device, &ring, 1, binding, irqevent, config, &dma, VBLK_READ, stride, 1,
           config->readonly ? 0xa7 : 0x51, &prior, NULL);
  if (!config->readonly) {
    testflush(&queue, binding, irqevent, 2 * stride);
    transfer(device, &ring, 1, binding, irqevent, config, &dma, VBLK_READ, 2 * stride, 4, 0x37,
             NULL, NULL);
  }
  resetpending(&queue, binding, irqevent, config, ringaddr);
  REQUIRE(!prepareblk(device, &fresh) && fresh.sectors == config->sectors &&
              fresh.readonly == config->readonly && fresh.flush == config->flush &&
              fresh.blocksize == config->blocksize && fresh.physical == config->physical &&
              fresh.alignment == config->alignment,
          183);
  {
    buffers[1].statusaddr = ringaddr[2];
    REQUIRE(blkqstart(&queue, device, &ring, 2, mapping, ringaddr, &ops, requests, buffers,
                      ARRAY_SIZE(requests)) == -ERR_INVALID &&
                !device->queues && device->state == VDEV_FEATURES && queue.state == BLKQ_STOPPED &&
                ring.queue.generation == 1,
            466);
    testbuffer(&dma, 6, &buffers[1]);
    REQUIRE(!blkqstart(&queue, device, &ring, 2, mapping, ringaddr, &ops, requests, buffers,
                       ARRAY_SIZE(requests)) &&
                blkqmatch(&queue, &prior) == -ERR_STALE,
            230);
  }
  REQUIRE(vrpoll(&ring, 1, stale, &count, &more) == -ERR_STALE && !count && !more, 184);
  REQUIRE(prior.generation == 1, 191);
  transfer(device, &ring, 2, binding, irqevent, &fresh, &dma, VBLK_READ,
           config->readonly ? stride : 2 * stride, 1, config->readonly ? 0xa7 : 0x37, &prior, NULL);
  REQUIRE(prior.generation == 2, 192);
  if (!config->readonly) {
    testredo(&queue, binding, irqevent, &fresh);
    testwriteback(device, &ring, binding, irqevent, &fresh, &dma);
    testparallel(&queue, binding, irqevent, &fresh);
    testgrowth(&queue, binding, irqevent);
    testresize(&queue, binding, irqevent, &fresh);
    blkrpc(&queue, binding, irqevent);
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
  Vmmio      mmio;
  u32        i, status;
  Handle     call = bootresource(sysboot(), VTEST_BLK_CALL);
  UNUSED(argc);
  UNUSED(argv);
  if (call > 0) {
    blkclient(sysboot(), call);
    return 42;
  }
  REQUIRE(memory > 0 && vdev_trace[1] && vdev_trace[1] <= 8, 151);
  mapping.mem = memory;
  mapping.offset = 0;
  mapping.address = 0x2000000;
  mapping.length = PAGE_SIZE;
  mapping.prot = MAP_READ | MAP_WRITE;
  mapping.reserved = 0;
  REQUIRE(!__sys_map(space, &mapping) && !__sys_free(memory), 152);
  for (i = 0; i < vdev_trace[1]; i++) {
    REQUIRE(vdev_trace[4 + i] <= PAGE_SIZE - 0x100 && vdev_trace[23 + i] >= 0x100, 153);
    REQUIRE(vdev_trace[23 + i] <= PAGE_SIZE - vdev_trace[4 + i] &&
                !vmmioinit(&mmio, &ops, mapping.address + vdev_trace[4 + i], vdev_trace[23 + i]),
            470);
    status = 0xf012;
    REQUIRE(ops.read(ops.context, (u32) mmio.bytes, &status) == -ERR_INVALID && status == 0xf012 &&
                ops.read(ops.context, 1, &status) == -ERR_INVALID && status == 0xf012,
            471);
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
    if (device.device == VBLK_DEVICE) {
      REQUIRE(vdev_trace[4 + i] <= PAGE_SIZE - 0x11a && vdev_trace[23 + i] >= 0x11a, 163);
      REQUIRE(!prepareblk(&device, &config), 164);
      vdev_trace[16]++;
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
      REQUIRE(blkqprepare(&device, now(), &config) == -ERR_NOTSUP && device.state == VDEV_FOUND,
              469);
      reset(&device);
      REQUIRE(!vfeatures(&device, 0, 0) && device.features == VDEV_VERSION1, 157);
      vdev_trace[17]++;
    }
    REQUIRE(device.state == VDEV_FEATURES && (device.features & VDEV_VERSION1) &&
                (device.offered & device.features) == device.features,
            167);
    REQUIRE(!virtioread(&mmio, 0x70, &status) && status == 11, 158);
    vdev_trace[15]++;
    if (device.device == VBLK_DEVICE)
      testblock(&device, &config, i);
    reset(&device);
    REQUIRE(!virtioread(&mmio, 0x70, &status) && !status, 159);
  }
  if (!vdev_trace[VTEST_BLK_RPC]) {
    Ipcmsg input;
    Msg    reply = {{(u64) -ERR_NOTSUP, 1, 0, 0}};
    REQUIRE(!ipc_recv(0, 0, &input) && input.kind == IPC_CALL &&
                input.words[0] == BLK_WORD(BLK_HELLO) && !ipc_reply(input.request, &reply),
            377);
  }
  REQUIRE(!__sys_unmap(space, mapping.address), 160);
  vdev_trace[0] = 99;
  return 42;
}
