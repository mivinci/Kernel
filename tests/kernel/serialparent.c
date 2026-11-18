/* Actual native parent transaction; never sends a service READY. */
#include <string.h>
#include "private/elf.h"
#include "private/start.h"
#include "private/serial.h"
#include <proto/serial.h>
#include <abi/resource.h>
#include <abi/system.h>
extern const u8             serial_verify[], serial_verify_end[];
volatile u64                serialproof[8];
typedef struct Serialpacket Serialpacket;
struct Serialpacket
{
  Boot     header;
  Strref   args[7];
  Resentry resources[8];
  char     strings[128];
};
#define CHECK(c, n)                                                                                \
  do {                                                                                             \
    if (!(c))                                                                                      \
      return n;                                                                                    \
  } while (0)
static isize
readimage(void *context, u64 offset, void *out, u64 bytes)
{
  u64 length = (u64) (serial_verify_end - serial_verify);
  (void) context;
  if (offset > length || bytes > length - offset)
    return -ERR_INVALID;
  memcpy(out, serial_verify + offset, bytes);
  return 0;
}
static void
stringarg(Serialpacket *packet, u32 index, const char *text, u32 *used)
{
  u32 bytes = (u32) strlen(text) + 1;
  packet->args[index].offset = (u32) offsetof(Serialpacket, strings) + *used;
  packet->args[index].length = bytes;
  memcpy(packet->strings + *used, text, bytes);
  *used += bytes;
}
static void
numberarg(Serialpacket *packet, u32 index, u64 number, u32 *used)
{
  char text[21];
  u32  position = 20;
  text[position] = 0;
  do {
    text[--position] = (char) ('0' + number % 10);
    number /= 10;
  } while (number);
  stringarg(packet, index, text + position, used);
}
static isize
mapping(Handle space, Handle memory, u64 bytes)
{
  Maparg args;
  args.mem = memory;
  args.offset = 0;
  args.address = 0x3000000;
  args.length = bytes;
  args.prot = MAP_READ | MAP_WRITE;
  args.reserved = 0;
  return __sys_map(space, &args);
}
int
serialparent(Handle creator, Handle listener, Handle mmio, Handle irq, u64 kind, u64 offset,
             u64 bytes, u64 shift, u64 width)
{
  Serialpacket packet;
  Elfsource    source;
  Elfplan      plan;
  Elfwork      work;
  Createarg    create;
  Config       config;
  Allocarg     alloc;
  Queryrec     state;
  Handle       pid, control, call, io, binding, rings[2], remote[2], collected, space;
  u64          reason, code, sequence, written, next;
  u32          i, turns, used = 0;
  u8           data[SERIAL_BATCH];
  memset(&work, 0, sizeof(work));
  memset(&create, 0, sizeof(create));
  create.memlimit = 64;
  create.exec_limit = 128;
  create.resources = 8;
  create.pending = 1;
  CHECK(!__sys_create(creator, &create, &work.view, &pid, &control) && control, 150);
  work.pid = pid;
  work.image = 1;
  space = work.parent_space = bootresource(sysboot(), BOOT_SELF_SPACE);
  work.scratch = 0x2000000;
  work.scratch_bytes = BOOT_MAX_BYTES;
  CHECK(!__sys_sigmask(SIGNAL_BLOCK, 0, &work.sigmask), 151);
  CHECK(!__sys_grant(work.view, listener, RESOURCE_USE, 0, 0, 0, &call), 152);
  CHECK(!__sys_grant(work.view, mmio, MAP_READ | MAP_WRITE, 0, ALIGN_UP(offset + bytes, PAGE_SIZE),
                     0, &io),
        153);
  CHECK(!__sys_irqbind(control, irq, 16, &binding), 154);
  memset(&alloc, 0, sizeof(alloc));
  alloc.bytes = SERIAL_RING_BYTES;
  alloc.kind = ALLOC_SHARED;
  for (i = 0; i < 2; i++) {
    CHECK(!__sys_alloc(bootresource(sysboot(), BOOT_SELF_ALLOC), &alloc, &rings[i]), 155);
    CHECK(!mapping(space, rings[i], SERIAL_RING_BYTES) && !serialinit((Serialring *) 0x3000000, 1),
          156);
    if (!i)
      CHECK(serialwrite((Serialring *) 0x3000000, 1, "boot-rx", 7) == 7, 157);
    CHECK(!__sys_unmap(space, 0x3000000), 158);
    CHECK(!__sys_grant(work.view, rings[i], MAP_READ | MAP_WRITE, 0, SERIAL_RING_BYTES, 0,
                       &remote[i]),
          159);
  }
  memset(&packet, 0, sizeof(packet));
  packet.header.magic = BOOT_MAGIC;
  packet.header.format = BOOT_FORMAT;
  packet.header.bytes = sizeof(packet);
  packet.header.argc = 7;
  packet.header.argv_off = offsetof(Serialpacket, args);
  packet.header.resource_count = 8;
  packet.header.resources_off = offsetof(Serialpacket, resources);
  packet.header.user_lo = BOOT_USER_LO;
  packet.header.user_hi = BOOT_USER_HI;
  packet.resources[0].tag = BOOT_SELF_SPACE;
  packet.resources[1].tag = BOOT_SELF_ALLOC;
  packet.resources[2].tag = BOOT_CREATE;
  packet.resources[3].tag = SERIAL_INIT_CALL;
  packet.resources[3].handle = call;
  packet.resources[4].tag = SERIAL_MMIO;
  packet.resources[4].handle = io;
  packet.resources[5].tag = SERIAL_IRQ_BIND;
  packet.resources[5].handle = binding;
  packet.resources[6].tag = SERIAL_RX_RING;
  packet.resources[6].handle = remote[0];
  packet.resources[7].tag = SERIAL_TX_RING;
  packet.resources[7].handle = remote[1];
  stringarg(&packet, 0, "serial-verify", &used);
  stringarg(&packet, 1, kind == BOOT_UART_NS16550 ? "ns16550" : "pl011", &used);
  numberarg(&packet, 2, offset, &used);
  numberarg(&packet, 3, bytes, &used);
  numberarg(&packet, 4, 7, &used);
  numberarg(&packet, 5, shift, &used);
  numberarg(&packet, 6, width, &used);
  CHECK(used <= sizeof(packet.strings), 160);
  source.context = NULL;
  source.bytes = (u64) (serial_verify_end - serial_verify);
  source.read = readimage;
  CHECK(!elfcheck(&source, create.memlimit, &plan), 161);
  CHECK(!elfload(&source, &plan, &packet, sizeof(packet), &work, &config) && !work.memory &&
            !work.mapped,
        162);
  CHECK(!__sys_configure(work.view, &config, 0) && !__sys_start(work.view), 163);
  CHECK(!__sys_wait(pid, 0, &collected, &reason, &code, &sequence) && collected == pid, 164);
  if (reason != WAIT_EXITED || code != 42)
    return reason == WAIT_EXITED && code ? (int) code : 165;
  /* Yield always leaves this parent READY: retirement must not require idle.
     Keep exact Life CONTROL until PROC query confirms the Run disappeared. */
  for (turns = 0; turns < 4096; turns++) {
    CHECK(!__sys_query(control, QUERY_PROC, 0, &state, 1, &written, &next) && !next, 171);
    if (!written)
      break;
    CHECK(written == 1 && state.pid == pid && state.data[3] == PROCESS_EXITING, 172);
    CHECK(!__sys_yield(), 173);
  }
  CHECK(turns < 4096, 174);
  serialproof[0] = (u64) pid;
  serialproof[1] = turns + 1;
  CHECK(!__sys_free(control), 166);
  for (i = 0; i < 2; i++) {
    CHECK(!mapping(space, rings[i], SERIAL_RING_BYTES), 167);
    if (i)
      CHECK(serialread((Serialring *) 0x3000000, 1, data, SERIAL_BATCH) == 7 &&
                !memcmp(data, "boot-tx", 7),
            168);
    CHECK(((Serialring *) 0x3000000)->producer == 7 && ((Serialring *) 0x3000000)->consumer == 7,
          169);
    CHECK(!__sys_unmap(space, 0x3000000) && !__sys_free(rings[i]), 170);
  }
  serialproof[2] = 1;
  serialproof[3] = serialproof[4] = 7;
  serialproof[5] = 7;
  serialproof[6] = kind;
  serialproof[7] = 1;
  return 0;
}
