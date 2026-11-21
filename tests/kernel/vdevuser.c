#include <unistd.h>
#include <sys/channel.h>
#include <abi/time.h>
#include <abi/syscall.h>
#include <abi/irq.h>
#include "private/syscall.h"
#include "private/start.h"
#include "private/virtio/block.h"
#include "virtiouser.h"
volatile u64 vdev_trace[VTEST_TRACE];
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
static void
transfer(Vdev *device, Vring *ring, u64 epoch, Handle binding, u64 irqevent,
         const Vblkconfig *config, u32 type, u64 sector, u64 cookie, u8 seed, Vdone *prior)
{
  Vblkreq      request = {0};
  Vdone        done[VQ_BATCH];
  Ipcmsg       event;
  Vblkhead    *header = (void *) (VTEST_DMA_BASE + 3 * PAGE_SIZE);
  volatile u8 *data = (void *) (VTEST_DMA_BASE + 4 * PAGE_SIZE);
  volatile u8 *status = (void *) (VTEST_DMA_BASE + 5 * PAGE_SIZE);
  u32          bytes = type == VBLK_FLUSH ? 0 : VBLK_SECTOR;
  u32          count, more, i, causes;
  u64          generation, started;
  for (i = 0; i < bytes; i++)
    data[i] = type == VBLK_WRITE ? (u8) (seed + i * 37) : 0;
  started = now();
  REQUIRE(started <= UINT64_MAX - VDEV_RESET_NS &&
              !__sys_alarmset(started + VDEV_RESET_NS, &generation) && !__sys_irqack(binding),
          179);
  REQUIRE(!vblksubmit(&request, ring, config, type, sector, cookie, vdev_trace[35],
                      bytes ? vdev_trace[36] : 0, bytes, vdev_trace[37], header, status, blksync,
                      device),
          174);
  REQUIRE(!ipc_recv(irqevent | EVENT_TIMER, RECV_EVENTS_ONLY, &event) && event.kind == IPC_EVENT &&
              event.words[0] == irqevent,
          176);
  REQUIRE(!__sys_alarmcancel(&generation), 180);
  /* The binding masks the line on delivery. Clear the device cause before
     consuming used entries; the next request rearms only after this drains. */
  REQUIRE(!vinterrupt(device, &causes) && causes == VDEV_IRQ_USED &&
              !virtioread(device->ops.context, 0x60, &causes) && !causes,
          181);
  if (prior && prior->generation)
    REQUIRE(prior->cookie == cookie && vblkfinish(&request, prior) == -ERR_STALE &&
                request.active && request.held == 7,
            190);
  REQUIRE(!vrpoll(ring, epoch, done, &count, &more), 175);
  REQUIRE(count == 1 && !more && done[0].cookie == cookie && ring->queue.free == VQ_SIZE &&
              vblkfinish(&request, &done[0]) == VBLK_OK && !request.held,
          177);
  if (prior)
    *prior = done[0];
  vdev_trace[38]++;
  if (type == VBLK_READ) {
    for (i = 0; i < bytes; i++)
      REQUIRE(data[i] == (u8) (seed + i * 37), 178);
    vdev_trace[22]++;
  } else if (type == VBLK_WRITE)
    vdev_trace[2]++;
  else
    vdev_trace[3]++;
}
static void
startqueue(Vdev *device, Vring *ring, u64 epoch)
{
  Vringops ops;
  u8      *desc = (void *) VTEST_DMA_BASE;
  ops.context = device;
  ops.sync = coherentsync;
  ops.fence = virtiofence;
  ops.notify = notify;
  REQUIRE(!vrinit(ring, epoch, desc, desc + PAGE_SIZE, desc + 2 * PAGE_SIZE, &ops), 170);
  REQUIRE(!vqueue(device, 0, vdev_trace[32], vdev_trace[33], vdev_trace[34]), 171);
  REQUIRE(!vstart(device), 172);
}
static void
resetpending(Vdev *device, Vring *ring, Handle binding, u64 irqevent, const Vblkconfig *config)
{
  Vblkreq      request = {0};
  Vblkhead    *header = (void *) (VTEST_DMA_BASE + 3 * PAGE_SIZE);
  volatile u8 *status = (void *) (VTEST_DMA_BASE + 5 * PAGE_SIZE);
  Ipcmsg       event;
  u64          generation, started = now();
  u32          causes;

  REQUIRE(started <= UINT64_MAX - VDEV_RESET_NS &&
              !__sys_alarmset(started + VDEV_RESET_NS, &generation) && !__sys_irqack(binding),
          185);
  REQUIRE(!vblksubmit(&request, ring, config, VBLK_READ, 1, 6, vdev_trace[35], vdev_trace[36],
                      VBLK_SECTOR, vdev_trace[37], header, status, blksync, device),
          186);
  REQUIRE(!ipc_recv(irqevent | EVENT_TIMER, RECV_EVENTS_ONLY, &event) && event.kind == IPC_EVENT &&
              event.words[0] == irqevent && !__sys_alarmcancel(&generation),
          187);
  /* Leave the device cause and used entry unconsumed. The IRQ alone does
     not return ownership of the request buffers. */
  REQUIRE(!virtioread(device->ops.context, 0x60, &causes) && causes == VDEV_IRQ_USED &&
              request.active && request.held == 7 && ring->queue.free == VQ_SIZE - 3,
          188);
  reset(device);
  REQUIRE(!virtioread(device->ops.context, 0x60, &causes) && !causes && !vblkreclaim(&request) &&
              !request.active && !request.held,
          189);
  vdev_trace[39]++;
}
static void
testblock(Vdev *device, const Vblkconfig *config, u32 member)
{
  Vring      ring;
  Vdone      stale[VQ_BATCH];
  Vdone      prior = {0};
  Vblkconfig fresh;
  u32        count, more;
  Handle     binding = bootresource(sysboot(), VTEST_IRQ_TAG + member);
  u64        irqevent = UINT64_C(1) << (IRQ_BIT_MIN + member);

  REQUIRE(binding > 0, 182);
  startqueue(device, &ring, 1);
  transfer(device, &ring, 1, binding, irqevent, config, VBLK_READ, 1, 1,
           config->readonly ? 0xa7 : 0x51, &prior);
  if (!config->readonly) {
    transfer(device, &ring, 1, binding, irqevent, config, VBLK_WRITE, 2, 2, 0x37, NULL);
    transfer(device, &ring, 1, binding, irqevent, config, VBLK_FLUSH, 0, 3, 0, NULL);
    transfer(device, &ring, 1, binding, irqevent, config, VBLK_READ, 2, 4, 0x37, NULL);
  }
  resetpending(device, &ring, binding, irqevent, config);
  REQUIRE(!vblkfeatures(device) && !vblkconfig(device, &fresh) &&
              fresh.sectors == config->sectors && fresh.readonly == config->readonly &&
              fresh.flush == config->flush,
          183);
  startqueue(device, &ring, 2);
  REQUIRE(vrpoll(&ring, 1, stale, &count, &more) == -ERR_STALE && !count && !more, 184);
  REQUIRE(prior.generation == 1, 191);
  transfer(device, &ring, 2, binding, irqevent, &fresh, VBLK_READ, config->readonly ? 1 : 2, 1,
           config->readonly ? 0xa7 : 0x37, &prior);
  REQUIRE(prior.generation == 2, 192);
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
      REQUIRE(vdev_trace[4 + i] <= PAGE_SIZE - 0x108 && vdev_trace[23 + i] >= 0x108, 163);
      REQUIRE(!vblkfeatures(&device) && !vblkconfig(&device, &config), 164);
      if (config.readonly) {
        REQUIRE((device.features & VBLK_F_RO) && config.sectors == 128, 165);
        vdev_trace[19]++;
        vdev_trace[21] = config.sectors;
      } else {
        REQUIRE(!(device.features & VBLK_F_RO) && config.flush && config.sectors == 256, 166);
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
