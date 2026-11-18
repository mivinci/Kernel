/* Native startup/authority/shared-memory fixture, not a serial service. */
#include "../../sbin/serial/start.h"
#include "private/start.h"
#include "private/syscall.h"
#include "private/serial.h"
#include <string.h>
#define CHECK(c, n)                                                                                \
  do {                                                                                             \
    if (!(c))                                                                                      \
      return n;                                                                                    \
  } while (0)
static isize
map(Serialboot *boot, u32 slot, u64 address, u64 bytes)
{
  Maparg args;
  args.mem = boot->handle[slot];
  args.offset = 0;
  args.address = address;
  args.length = bytes;
  args.prot = MAP_READ | MAP_WRITE;
  args.reserved = 0;
  return __sys_map(boot->handle[SerialSpace], &args);
}
int
main(int argc, char **argv)
{
  Serialboot boot, before;
  u8         data[SERIAL_BATCH];
  isize      error;
  (void) argv;
  CHECK(argc == 7 && !serialboot(sysboot(), &boot), 201);
  error = serialcheck(&boot);
  if (error)
    return (int) (220 - error);
  CHECK(boot.pid > 1 && boot.image == 1 && boot.initimage == 1 && boot.number == 7, 202);
  CHECK(!map(&boot, SerialMmio, 0x2000000, boot.mapbytes), 203);
  CHECK(!map(&boot, SerialRx, 0x2100000, SERIAL_RING_BYTES) &&
            !map(&boot, SerialTx, 0x2200000, SERIAL_RING_BYTES),
        204);
  CHECK(serialread((Serialring *) 0x2100000, 1, data, SERIAL_BATCH) == 7 &&
            !memcmp(data, "boot-rx", 7),
        205);
  CHECK(serialwrite((Serialring *) 0x2200000, 1, "boot-tx", 7) == 7, 206);
  CHECK(!__sys_unmap(boot.handle[SerialSpace], 0x2000000) &&
            !__sys_unmap(boot.handle[SerialSpace], 0x2100000) &&
            !__sys_unmap(boot.handle[SerialSpace], 0x2200000),
        207);
  before = boot;
  CHECK(!__sys_free(boot.handle[SerialIrq]) && serialcheck(&boot) == -ERR_DENIED &&
            !memcmp(&boot, &before, sizeof(boot)),
        208);
  return 42;
}
