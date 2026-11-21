#include <unistd.h>
#include <sys/channel.h>
#include <abi/time.h>
#include <abi/syscall.h>
#include "private/syscall.h"
#include "private/start.h"
#include "private/virtio/block.h"
#include "virtiouser.h"
volatile u64 vdev_trace[32];
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
    reset(&device);
    REQUIRE(!virtioread(&base, 0x70, &status) && !status, 159);
  }
  REQUIRE(!__sys_unmap(space, mapping.address), 160);
  vdev_trace[0] = 99;
  return 42;
}
