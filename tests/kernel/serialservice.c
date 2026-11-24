/* PID1 fixture: real serial READY/binding/IRQ I/O, no production init shortcut. */
#include <sys/boot.h>
#include <sys/channel.h>
#include <proto/serial.h>
#include <proto/init.h>
#include <proto/file.h>
#include <errno.h>
#include <proto/tty.h>
#include <abi/resource.h>
#include <abi/system.h>
#include <abi/time.h>
#include <unistd.h>
#include <string.h>
#include "private/start.h"
#include "private/syscall.h"
#include "private/serial.h"
#include "private/elf.h"

extern const u8 serial_service[], serial_service_end[], serial_peer[], serial_peer_end[];
extern const u8 tty_service[], tty_service_end[];
volatile u64    serialserviceproof[16];

typedef struct Packet Packet;
typedef struct Child  Child;
struct Packet
{
  Boot     header;
  Strref   args[7];
  Resentry resources[8];
  char     strings[128];
};
struct Child
{
  Handle    pid, control;
  Elfwork   work;
  Elfsource source;
  Elfplan   plan;
  Config    config;
  Packet    packet;
  const u8 *data;
  u64       bytes;
};
static Child  driver, client;
static Handle creator, listener, system, memory, source, rings[2], drivercall;
static u64    image, kind, offset, bytes, shift, width;

static NORETURN void
failed(u64 stage, isize error)
{
  serialserviceproof[14] = stage;
  serialserviceproof[15] = (u64) error;
  _exit((int) stage);
}
#define CHECK(c, n)                                                                                \
  do {                                                                                             \
    if (!(c))                                                                                      \
      failed(n, 0);                                                                                \
  } while (0)

static void
inventory(void)
{
  const Bootview *boot = sysboot();
  Queryrec        rows[4];
  u64             cursor = 0, written, next, i;
  Handle          pid, parent;
  CHECK(!__sys_getid(&pid, &image, &parent) && pid == 1 && !parent && image == 1, 100);
  creator = bootresource(boot, BOOT_CREATE);
  system = bootresource(boot, BOOT_SYSTEM);
  CHECK(creator && system, 101);
  do {
    CHECK(!__sys_query(0, QUERY_RESOURCE, cursor, rows, ARRAY_SIZE(rows), &written, &next), 102);
    for (i = 0; i < written; i++)
      if (rows[i].data[1] == RESOURCE_CALL && rows[i].data[6] == 1 && rows[i].data[7] == image &&
          rows[i].data[2] == (RESOURCE_USE | RESOURCE_GRANT))
        listener = (Handle) rows[i].data[0];
    cursor = next;
  } while (cursor);
  CHECK(listener && !__sys_query(system, QUERY_BOOT_RESOURCE, 0, rows, 1, &written, &next) &&
            written == 1 && !next,
        103);
  memory = (Handle) rows[0].data[0];
  kind = rows[0].data[1];
  offset = rows[0].data[2] & PAGE_MASK;
  bytes = rows[0].data[3];
  source = (Handle) rows[0].data[4];
  shift = rows[0].data[6];
  width = rows[0].data[7];
  CHECK(memory && source && !rows[0].data[5], 104);
}

static void
stringarg(Packet *packet, u32 index, const char *text, u32 *used)
{
  usize length = strlen(text) + 1;
  CHECK(length <= sizeof(packet->strings) - *used, 105);
  packet->args[index].offset = (u32) offsetof(Packet, strings) + *used;
  packet->args[index].length = (u32) length;
  memcpy(packet->strings + *used, text, length);
  *used += (u32) length;
}

static void
numberarg(Packet *packet, u32 index, u64 value, u32 *used)
{
  char text[21];
  u32  position = 20;
  text[position] = 0;
  do {
    text[--position] = (char) ('0' + value % 10);
    value /= 10;
  } while (value);
  stringarg(packet, index, text + position, used);
}

static void
newchild(Child *child, const u8 *data, u64 length, u64 argc, u64 resources)
{
  Createarg args;
  memset(&args, 0, sizeof(args));
  args.memlimit = 64;
  args.exec_limit = 128;
  args.resources = 16;
  args.pending = 4;
  CHECK(!__sys_create(creator, &args, &child->work.view, &child->pid, &child->control), 106);
  child->data = data;
  child->bytes = length;
  child->work.pid = child->pid;
  child->work.image = 1;
  child->work.parent_space = bootresource(sysboot(), BOOT_SELF_SPACE);
  child->work.scratch = 0x2000000;
  child->work.scratch_bytes = BOOT_MAX_BYTES;
  CHECK(!__sys_sigmask(SIGNAL_BLOCK, 0, &child->work.sigmask), 107);
  child->packet.header.magic = BOOT_MAGIC;
  child->packet.header.format = BOOT_FORMAT;
  child->packet.header.bytes = sizeof(child->packet);
  child->packet.header.argc = argc;
  child->packet.header.argv_off = offsetof(Packet, args);
  child->packet.header.resource_count = resources;
  child->packet.header.resources_off = offsetof(Packet, resources);
  child->packet.header.user_lo = BOOT_USER_LO;
  child->packet.header.user_hi = BOOT_USER_HI;
  child->packet.resources[0].tag = BOOT_SELF_SPACE;
  child->packet.resources[1].tag = BOOT_SELF_ALLOC;
}

static void
grant(Child *child, u32 index, u64 tag, Handle handle, u64 rights, u64 length)
{
  Resentry *entry = &child->packet.resources[index];
  entry->tag = tag;
  CHECK(!__sys_grant(child->work.view, handle, rights, 0, length, 0, &entry->handle), 108);
}

static isize
readimage(void *context, u64 offset, void *out, u64 length)
{
  Child *child = context;
  if (offset > child->bytes || length > child->bytes - offset)
    return -ERR_INVALID;
  memcpy(out, child->data + offset, length);
  return 0;
}

static void
start(Child *child)
{
  child->source.context = child;
  child->source.bytes = child->bytes;
  child->source.read = readimage;
  CHECK(!elfcheck(&child->source, 64, &child->plan) &&
            !elfload(&child->source, &child->plan, &child->packet, sizeof(child->packet),
                     &child->work, &child->config),
        109);
  CHECK(!__sys_configure(child->work.view, &child->config, 0) && !__sys_start(child->work.view),
        110);
}

static void
mapping(Handle memory, u64 address)
{
  Maparg args;
  args.mem = memory;
  args.offset = 0;
  args.address = address;
  args.length = SERIAL_RING_BYTES;
  args.prot = MAP_READ | MAP_WRITE;
  args.reserved = 0;
  CHECK(!__sys_map(bootresource(sysboot(), BOOT_SELF_SPACE), &args), 111);
}

static void
receive(Handle pid, u64 opcode, Ipcmsg *message)
{
  for (;;) {
    CHECK(!ipc_recv(EVENT_CHILD | EVENT_TIMER | EVENT_RQ_CANCEL, 0, message), 112);
    if (message->kind == IPC_CALL) {
      CHECK(message->source == pid && message->image == 1 && message->words[0] == opcode, 113);
      return;
    }
    if (message->kind == IPC_EVENT && message->words[0] == EVENT_RQ_CANCEL)
      continue;
    if (message->kind == IPC_EVENT && message->words[0] & EVENT_CHILD) {
      Handle child;
      u64    reason, code, sequence;
      isize  error = __sys_wait(-1, WAIT_WNOHANG, &child, &reason, &code, &sequence);
      if (!error && child)
        failed(114, (isize) code);
      CHECK(!error || error == -ERR_CHILD, 114);
      if (!(message->words[0] & EVENT_TIMER))
        continue;
    }
    serialserviceproof[8] = message->kind;
    serialserviceproof[9] = (u64) message->source;
    serialserviceproof[10] = (u64) message->request;
    serialserviceproof[11] = message->words[0];
    serialserviceproof[12] = message->words[1];
    failed(115, -ERR_TIMEDOUT);
  }
}

static void
reply(Handle request, u64 seq, u64 a0, u64 a1)
{
  Msg words;
  words.words[0] = 0;
  words.words[1] = seq;
  words.words[2] = a0;
  words.words[3] = a1;
  CHECK(!ipc_reply(request, &words), 116);
}

static void
controlcall(u64 opcode, u64 sequence, u64 a0, u64 a1, isize expected, Ipcmsg *message)
{
  Msg words;
  words.words[0] = SERIAL_WORD(opcode);
  words.words[1] = sequence;
  words.words[2] = a0;
  words.words[3] = a1;
  CHECK(!ipc_call(drivercall, &words, CALL_CONTROL, message) &&
            (isize) message->words[0] == expected && message->words[1] == sequence,
        117);
}

static void
waitchild(Child *child, u64 expected, u64 code)
{
  Handle collected;
  u64    reason, status, sequence;
  CHECK(!__sys_wait(child->pid, 0, &collected, &reason, &status, &sequence) &&
            collected == child->pid && reason == expected && status == code,
        118);
}

static void
retired(Child *child)
{
  Queryrec row;
  u64      written, next;
  u32      attempts;
  for (attempts = 0; attempts < 4096; attempts++) {
    CHECK(!__sys_query(child->control, QUERY_PROC, 0, &row, 1, &written, &next) && !next, 119);
    if (!written)
      break;
    CHECK(row.pid == child->pid && row.data[3] == PROCESS_EXITING && !__sys_yield(), 120);
  }
  CHECK(attempts < 4096 && !__sys_free(child->control), 121);
}

static void
idle(u64 received, u64 transmitted, u32 flag)
{
  Queryrec row, peer;
  u64      written, next, nanos;
  int64_t  seconds, start;
  CHECK(!__sys_clock_gettime(ABI_CLOCK_MONOTONIC, &start, &nanos), 148);
  for (;;) {
    CHECK(!__sys_query(driver.control, QUERY_PROC, 0, &row, 1, &written, &next) && written == 1 &&
              !next && row.pid == driver.pid && row.image == 1 && row.data[3] != PROCESS_EXITING &&
              row.data[1] == (u64) driver.pid && row.data[2] == (u64) driver.pid,
          146);
    CHECK(!__sys_query(client.control, QUERY_PROC, 0, &peer, 1, &written, &next) && written == 1 &&
              !next && peer.pid == client.pid && peer.image == 1 && peer.data[3] != PROCESS_EXITING,
          150);
    if (__atomic_load_n(&((Serialring *) 0x3000000)->consumer, __ATOMIC_ACQUIRE) == received &&
        __atomic_load_n(&((Serialring *) 0x3010000)->consumer, __ATOMIC_ACQUIRE) == transmitted &&
        row.data[3] == PROCESS_BLOCKED && !row.data[4] && peer.data[3] == PROCESS_BLOCKED &&
        !peer.data[4])
      break;
    CHECK(!__sys_yield() && !__sys_clock_gettime(ABI_CLOCK_MONOTONIC, &seconds, &nanos) &&
              seconds - start < 10,
          147);
  }
  serialserviceproof[flag] = 1;
}

int
main(int argc, char **argv)
{
  Ipcmsg   message, response;
  Allocarg alloc;
  Handle   binding, notify, installed, driver_request;
  u64      generation, seconds_ns;
  int64_t  seconds;
  u32      i, used = 0, tries;
  isize    error;
  int      timeout, tty;
  CHECK(argc == 1, 122);
  timeout = !strcmp(argv[0], "tests/serial-timeout");
  tty = !strcmp(argv[0], "tests/serial-tty");
  inventory();
  CHECK(!__sys_clock_gettime(ABI_CLOCK_MONOTONIC, &seconds, &seconds_ns) && seconds >= 0 &&
            (u64) seconds < (UINT64_MAX - seconds_ns) / UINT64_C(1000000000),
        123);
  CHECK(!__sys_alarmset((u64) seconds * UINT64_C(1000000000) + seconds_ns + UINT64_C(20000000000),
                        &generation),
        124);
  memset(&alloc, 0, sizeof(alloc));
  alloc.bytes = SERIAL_RING_BYTES;
  alloc.kind = ALLOC_SHARED;
  for (i = 0; i < 2; i++) {
    CHECK(!__sys_alloc(bootresource(sysboot(), BOOT_SELF_ALLOC), &alloc, &rings[i]), 125);
    mapping(rings[i], 0x3000000 + (u64) i * 0x10000);
    CHECK(!serialinit((Serialring *) (0x3000000 + (u64) i * 0x10000), 1), 126);
  }
  newchild(&driver, serial_service, (u64) (serial_service_end - serial_service), 7, 7);
  grant(&driver, 2, SERIAL_INIT_CALL, listener, RESOURCE_USE, 0);
  grant(&driver, 3, SERIAL_MMIO, memory, MAP_READ | MAP_WRITE, ALIGN_UP(offset + bytes, PAGE_SIZE));
  CHECK(!__sys_irqbind(driver.control, source, SERIAL_IRQ_BIT, &binding), 127);
  driver.packet.resources[4].tag = SERIAL_IRQ_BIND;
  driver.packet.resources[4].handle = binding;
  grant(&driver, 5, SERIAL_RX_RING, rings[0], MAP_READ | MAP_WRITE, SERIAL_RING_BYTES);
  grant(&driver, 6, SERIAL_TX_RING, rings[1], MAP_READ | MAP_WRITE, SERIAL_RING_BYTES);
  stringarg(&driver.packet, 0, "sbin/serial", &used);
  stringarg(&driver.packet, 1, kind == BOOT_UART_NS16550 ? "ns16550" : "pl011", &used);
  numberarg(&driver.packet, 2, offset, &used);
  numberarg(&driver.packet, 3, bytes, &used);
  numberarg(&driver.packet, 4, 7, &used);
  numberarg(&driver.packet, 5, shift, &used);
  numberarg(&driver.packet, 6, width, &used);
  for (tries = 0; tries < 64; tries++) {
    error = __sys_debughandoff(system);
    if (!error)
      break;
    CHECK(error == -ERR_BUSY && !__sys_yield(), 128);
  }
  CHECK(tries < 64, 129);
  start(&driver);
  CHECK(!__sys_connect(driver.control, CONNECT_CALL, RESOURCE_USE | RESOURCE_GRANT, 0, &drivercall),
        130);
  receive(driver.pid, INIT_WORD(INIT_READY), &message);
  CHECK(message.words[1] == 1 && message.words[2] == 7 && message.words[3] == 1, 131);
  serialserviceproof[0] = 1;
  serialserviceproof[1] = (u64) driver.pid;
  if (timeout) {
    driver_request = message.request;
    CHECK(!ipc_defer(driver_request), 132);
    controlcall(SERIAL_STATUS, 1, 0, 0, -ERR_BUSY, &response);
    waitchild(&driver, WAIT_EXITED, 106);
    CHECK(((Serialring *) 0x3000000)->flags == SERIAL_ERROR && !ipc_rqfinish(driver_request), 133);
    CHECK(!__sys_free(drivercall), 134);
    retired(&driver);
    serialserviceproof[2] = 2;
    serialserviceproof[3] = 106;
  } else {
    reply(message.request, 1, 7, 1);
    if (tty)
      newchild(&client, tty_service, (u64) (tty_service_end - tty_service), 2, 6);
    else
      newchild(&client, serial_peer, (u64) (serial_peer_end - serial_peer), 1, 7);
    used = 0;
    stringarg(&client.packet, 0, tty ? "sbin/tty" : "serial-test-peer", &used);
    if (tty)
      numberarg(&client.packet, 1, 8, &used);
    grant(&client, 2, tty ? TTY_INIT_CALL : 0x200, listener, RESOURCE_USE, 0);
    grant(&client, 3, tty ? TTY_RX_RING : 0x201, rings[0], MAP_READ | MAP_WRITE, SERIAL_RING_BYTES);
    grant(&client, 4, tty ? TTY_TX_RING : 0x202, rings[1], MAP_READ | MAP_WRITE, SERIAL_RING_BYTES);
    CHECK(!__sys_connect(driver.control, CONNECT_NOTIFY, RESOURCE_USE | RESOURCE_GRANT,
                         SERIAL_RX_ROOM | SERIAL_TX_READY, &notify),
          135);
    grant(&client, 5, tty ? TTY_SERIAL_NOTIFY : 0x203, notify, RESOURCE_USE, 0);
    CHECK(!__sys_free(notify), 136);
    if (!tty)
      grant(&client, 6, 0x204, drivercall, RESOURCE_USE, 0);
    start(&client);
    receive(client.pid, tty ? INIT_WORD(INIT_READY) : UINT64_C(0x53525001), &message);
    CHECK(message.words[1] == 1 && message.words[2] == (tty ? 8 : 1) &&
              message.words[3] == (tty ? 1 : (u64) client.pid),
          137);
    if (tty)
      reply(message.request, 1, 8, 1);
    CHECK(!__sys_connect(client.control, CONNECT_NOTIFY, RESOURCE_USE | RESOURCE_GRANT,
                         SERIAL_RX_READY | SERIAL_TX_ROOM, &notify) &&
              !__sys_grant(driver.control, notify, RESOURCE_USE, 0, 0, 0, &installed),
          138);
    CHECK(!__sys_free(notify), 139);
    controlcall(SERIAL_BIND, 1, (u64) installed, 2, -ERR_STALE, &response);
    controlcall(SERIAL_BIND, 2, (u64) installed, 1, 0, &response);
    CHECK(response.words[2] == (u64) client.pid && response.words[3] == 1, 140);
    controlcall(SERIAL_BIND, 2, (u64) installed, 1, 0, &response);
    controlcall(SERIAL_BIND, 3, (u64) installed, 1, -ERR_BUSY, &response);
    serialserviceproof[2] = tty ? 3 : 1;
    serialserviceproof[3] = (u64) client.pid;
    if (tty) {
      Handle call;
      Msg    hello = {{FS_HELLO, 0, 17, UINT64_MAX}};
      Msg    read = {{FS_READ, 0x10000, 23, 0}};
      CHECK(!__sys_connect(client.control, CONNECT_CALL, RESOURCE_USE, 0, &call), 160);
      CHECK(!ipc_call(call, &hello, CALL_RECV_MEM, &response) && !response.kind &&
                (isize) response.words[0] == -ENOTSUP && response.words[1] == hello.words[2] &&
                !response.words[2] && !response.words[3],
            161);
      CHECK(!ipc_call(call, &read, 0, &response) && !response.kind &&
                (isize) response.words[0] == -ENOTSUP && response.words[1] == read.words[2] &&
                !response.words[2] && !response.words[3] && !__sys_free(call),
            162);
      idle(0, 0, 8);
      idle(3, 3, 9);
      idle(8, 9, 13);
    } else {
      reply(message.request, 1, 0, 0);
      idle(0, 12, 8);
      receive(client.pid, UINT64_C(0x53525003), &message);
      CHECK(message.words[1] == 2 && message.words[2] == 3 && message.words[3] == 24, 149);
      reply(message.request, 2, 0, 0);
      idle(3, 24, 9);
      receive(client.pid, UINT64_C(0x53525002), &message);
      CHECK(message.words[1] == 3 && message.words[2] == 7 && message.words[3] == 24, 141);
    }
    controlcall(SERIAL_STATUS, 4, 0, 0, 0, &response);
    CHECK(response.words[2] == (tty ? 8 : 7) && response.words[3] == (tty ? 9 : 24), 142);
    serialserviceproof[4] = response.words[2];
    serialserviceproof[5] = response.words[3];
    CHECK(!__sys_free(drivercall) && !__sys_terminate(driver.control), 143);
    waitchild(&driver, WAIT_EXITED, WAIT_SIGNALLED | 9);
    if (tty) {
      CHECK(!__sys_terminate(client.control), 151);
      waitchild(&client, WAIT_EXITED, WAIT_SIGNALLED | 9);
    } else {
      reply(message.request, 3, 0, 0);
      waitchild(&client, WAIT_EXITED, 42);
    }
    retired(&client);
    retired(&driver);
  }
  for (i = 0; i < 2; i++) {
    CHECK(!__sys_unmap(bootresource(sysboot(), BOOT_SELF_SPACE), 0x3000000 + (u64) i * 0x10000) &&
              !__sys_free(rings[i]),
          144);
  }
  CHECK(!__sys_alarmcancel(&generation) && !__sys_free(source) && !__sys_free(memory) &&
            !__sys_free(system),
        145);
  serialserviceproof[6] = 1;
  serialserviceproof[7] = kind;
  return 42;
}
