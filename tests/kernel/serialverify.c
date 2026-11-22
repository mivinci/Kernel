/* Native startup/authority/shared-memory fixture, not a serial service. */
#include "../../sbin/serial/start.h"
#include "../../sbin/serial/io.h"
#include "private/start.h"
#include "private/syscall.h"
#include "private/serial.h"
#include <string.h>
#include <sys/channel.h>
static Serialring pumprx, pumptx;
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
static int
pumpcheck(const Serialboot *boot)
{
  Serialio io;
  Uart     uart;
  u64      hints, drained, seen = 0;
  u32      again, turns;
  isize    error;
  for (turns = 0; turns < 64; turns++) {
    error = uartinit(&uart, 0x2000000 + boot->offset, boot->bytes, (u32) boot->kind,
                     (u32) boot->shift, (u32) boot->width);
    if (!error)
      break;
    CHECK(error == -ERR_BUSY && !__sys_yield(), 209);
  }
  CHECK(turns < 64, 210);
  CHECK(!serialinit(&pumprx, 1) && !serialinit(&pumptx, 1) &&
            !serialioinit(&io, &uart, &pumprx, &pumptx, 1),
        211);
  CHECK(serialwrite(&pumptx, 1, "\n", 1) == 1, 212);
  CHECK(!serialiodrained(&io, &drained) && !drained, 236);
  for (turns = 0; turns < 64; turns++) {
    CHECK(!serialiostep(&io, &hints, &again), 213);
    seen |= hints;
    if (!io.count)
      break;
    CHECK(!__sys_yield(), 214);
  }
  CHECK(turns < 64 && seen == SERIAL_TX_ROOM && !pumprx.producer && pumptx.producer == 1 &&
            pumptx.consumer == 1 && uart.transmitted == 1 && !uart.received,
        215);
  CHECK(!serialiostep(&io, &hints, &again) && !hints && !again &&
            uart.mask == (boot->kind == BOOT_UART_NS16550 ? 5U : 0x7d0U),
        216);
  for (turns = 0; turns < 64; turns++) {
    error = serialiodrained(&io, &drained);
    if (!error)
      break;
    CHECK(error == -ERR_WOULD_BLOCK && !drained && !__sys_yield(), 237);
  }
  CHECK(turns < 64 && drained == 1, 238);
  CHECK(!uartmask(&uart, 0, 0), 217);
  return 0;
}
int
main(int argc, char **argv)
{
  Serialboot boot, before;
  u8         data[SERIAL_BATCH];
  isize      error;
  Ipcmsg     response;
  Msg        message = {{UINT64_C(0x54545901), 0, 0, 0}};
  Handle     terminal, pgid, sid;
  u64        epoch;
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
  error = pumpcheck(&boot);
  if (error)
    return (int) error;
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
  terminal = bootresource(sysboot(), 0x300);
  CHECK(terminal > 0 && !__sys_getfg(terminal, &pgid, &sid, &epoch) && !pgid && !sid && epoch == 1,
        232);
  CHECK(!__sys_setsid(&sid) && sid == boot.pid, 233);
  message.words[1] = (u64) sid;
  /* Keep this exact ACTIVE Image alive until the parent issues its terminal.
     This is fixture synchronization, unrelated to serial's READY protocol. */
  CHECK(!ipc_call(boot.handle[SerialInit], &message, 0, &response) &&
            response.words[0] == message.words[0] && !response.request,
        221);
  CHECK(!__sys_getfg(terminal, &pgid, &sid, &epoch) && !pgid && sid == boot.pid && epoch == 1 &&
            __sys_ttybind(terminal, sid) == -ERR_DENIED,
        234);
  CHECK(__sys_getfg(boot.handle[SerialInit], &pgid, &sid, &epoch) == -ERR_BAD_HANDLE && !pgid &&
            !sid && !epoch,
        235);
  return 42;
}
