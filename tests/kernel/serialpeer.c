/* Run the tty data path against the actual production serial ELF. */
#include <sys/boot.h>
#include <sys/channel.h>
#include <proto/serial.h>
#include "private/start.h"
#include "private/syscall.h"
#include "../../sbin/tty/serial.h"
#include <string.h>

#define PARENT_TAG 0x200
#define RX_TAG     0x201
#define TX_TAG     0x202
#define NOTIFY_TAG 0x203
#define CALL_TAG   0x204
#define PEER_READY UINT64_C(0x53525001)
#define PEER_DONE  UINT64_C(0x53525002)
#define PEER_MID   UINT64_C(0x53525003)
static Ttyinput      line;
static Ttyserial     transport;
static const Termarg mode = {0, 0, TTY_CS8, 0, 115200, 115200, {0}};
#define REQUIRE(c, n)                                                                              \
  do {                                                                                             \
    if (!(c))                                                                                      \
      return n;                                                                                    \
  } while (0)

static isize
map(Handle memory, u64 address)
{
  Maparg args;
  args.mem = memory;
  args.offset = 0;
  args.address = address;
  args.length = SERIAL_RING_BYTES;
  args.prot = MAP_READ | MAP_WRITE;
  args.reserved = 0;
  return __sys_map(bootresource(sysboot(), BOOT_SELF_SPACE), &args);
}

static int
sendoutput(Handle notify, const char *text, u32 bytes)
{
  Ttyserialresult result;
  return ttyserialoutput(&transport, mode.oflag, text, bytes) == bytes &&
         !ttyserialpump(&transport, &line, &mode, 0, notify, &result) &&
         result.hints == SERIAL_TX_READY && !result.again && !transport.queued;
}

int
main(int argc, char **argv)
{
  const Bootview *boot = sysboot();
  const char      input[] = "uart-rx", output[] = "serial-live\n", next[] = "serial-next\n";
  Handle          parent, pid, ancestor, driver, notify;
  u64             image;
  Ipcmsg          message;
  Msg             words;
  u8              data[SERIAL_BATCH];
  u32             received = 0, continued = 0, ended, wakeups = 0;
  isize           count;
  (void) argv;
  REQUIRE(argc == 1 && !__sys_getid(&pid, &image, &ancestor) && ancestor == 1, 201);
  parent = bootresource(boot, PARENT_TAG);
  driver = bootresource(boot, CALL_TAG);
  notify = bootresource(boot, NOTIFY_TAG);
  REQUIRE(parent && driver && notify && !map(bootresource(boot, RX_TAG), 0x2100000) &&
              !map(bootresource(boot, TX_TAG), 0x2200000) && !ttyinputinit(&line, 0) &&
              !ttyserialinit(&transport, (Serialring *) 0x2100000, (Serialring *) 0x2200000, 1),
          202);
  words.words[0] = SERIAL_WORD(SERIAL_BIND);
  words.words[1] = 1;
  words.words[2] = words.words[3] = 0;
  REQUIRE(!ipc_call(driver, &words, CALL_CONTROL, &message) &&
              (isize) message.words[0] == -ERR_DENIED && message.words[1] == 1 &&
              !message.words[2] && !message.words[3],
          203);
  words.words[0] = PEER_READY;
  words.words[1] = 1;
  words.words[2] = image;
  words.words[3] = (u64) pid;
  REQUIRE(!ipc_call(parent, &words, CALL_CONTROL, &message) && !message.words[0], 204);
  REQUIRE(sendoutput(notify, output, sizeof(output) - 1), 205);
  while (received < sizeof(input) - 1) {
    Ttyserialresult result;
    REQUIRE(!ttyserialpump(&transport, &line, &mode, SERIAL_BATCH, notify, &result) &&
                !result.end && !result.signal,
            206);
    count = ttyinputread(&line, data, SERIAL_BATCH, &ended);
    REQUIRE(count == -ERR_WOULD_BLOCK ||
                (count > 0 && !ended && (u64) count <= sizeof(input) - 1 - received &&
                 result.hints == SERIAL_RX_ROOM),
            206);
    if (count == -ERR_WOULD_BLOCK)
      count = 0;
    if (count) {
      REQUIRE(!memcmp(data, input + received, (usize) count), 207);
      received += (u32) count;
      if (received == 3 && !continued) {
        REQUIRE(sendoutput(notify, next, sizeof(next) - 1), 208);
        words.words[0] = PEER_MID;
        words.words[1] = 2;
        words.words[2] = received;
        words.words[3] = sizeof(output) + sizeof(next) - 2;
        REQUIRE(!ipc_call(parent, &words, CALL_CONTROL, &message) && !message.words[0], 211);
        continued = 1;
      }
      continue;
    }
    if (result.again) {
      REQUIRE(!__sys_yield(), 209);
      continue;
    }
    REQUIRE(!ipc_recv(SERIAL_RX_READY | SERIAL_TX_ROOM, RECV_EVENTS_ONLY, &message) &&
                message.kind == IPC_EVENT && !message.source && !message.request &&
                message.words[0] && !(message.words[0] & ~(SERIAL_RX_READY | SERIAL_TX_ROOM)),
            209);
    if (message.words[0] & SERIAL_RX_READY)
      wakeups++;
  }
  REQUIRE(wakeups >= 2, 212);
  words.words[0] = PEER_DONE;
  words.words[1] = 3;
  words.words[2] = received;
  words.words[3] = sizeof(output) + sizeof(next) - 2;
  REQUIRE(!ipc_call(parent, &words, CALL_CONTROL, &message) && !message.words[0], 210);
  return 42;
}
