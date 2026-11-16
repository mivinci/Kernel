#include <sys/boot.h>
#include <sys/channel.h>
#include "private/syscall.h"
static volatile u64       bss[257];
static volatile u64       initialized = UINT64_C(0x123456789abcdef0);
static const volatile u64 readonly = UINT64_C(0xfedcba9876543210);
#define REQUIRE(c, n)                                                                              \
  do {                                                                                             \
    if (!(c))                                                                                      \
      __sys_exit(n);                                                                               \
  } while (0)
static void
checkempty(const Ipcmsg *message, u32 request)
{
  REQUIRE(!message->kind && !message->source && !message->image, 101);
  if (!request)
    REQUIRE(!message->request, 102);
}
static void
memory(Handle allocator, Handle space)
{
  Allocarg      args;
  Maparg        map;
  Handle        handle;
  volatile u64 *bytes = (volatile u64 *) 0x800000;
  u32           i;
  args.bytes = PAGE_SIZE;
  args.kind = ALLOC_DATA;
  args.align = args.minaddr = args.maxaddr = args.reserved = 0;
  REQUIRE(!__sys_alloc(allocator, &args, &handle) && handle > 0, 103);
  map.mem = handle;
  map.offset = 0;
  map.address = (u64) bytes;
  map.length = PAGE_SIZE;
  map.prot = MAP_READ | MAP_WRITE;
  map.reserved = 0;
  REQUIRE(!__sys_map(space, &map), 104);
  for (i = 0; i < PAGE_SIZE / 8; i++) {
    REQUIRE(!bytes[i], 105);
    bytes[i] = i + initialized;
  }
  REQUIRE(bytes[511] == initialized + 511, 106);
  REQUIRE(!__sys_unmap(space, (u64) bytes) && !__sys_free(handle), 107);
  args.kind = 0;
  handle = 77;
  REQUIRE(__sys_alloc(allocator, &args, &handle) == -ERR_INVALID && !handle, 108);
}
static void
copycheck(Handle allocator, Handle space, const Boot *boot)
{
  Allocarg     args;
  Maparg       map;
  Handle       source, target, code;
  volatile u8 *input = (volatile u8 *) 0x900000;
  volatile u8 *output = (volatile u8 *) 0xb00000;
  u64          i;
  args.bytes = COPY_BYTES;
  args.kind = ALLOC_DATA;
  args.align = args.minaddr = args.maxaddr = args.reserved = 0;
  REQUIRE(!__sys_alloc(allocator, &args, &source) && !__sys_alloc(allocator, &args, &target), 134);
  map.offset = map.reserved = 0;
  map.length = COPY_BYTES;
  map.prot = MAP_READ | MAP_WRITE;
  map.mem = source;
  map.address = (u64) input;
  REQUIRE(!__sys_map(space, &map), 135);
  map.address = 0xa00000;
  REQUIRE(!__sys_map(space, &map), 136);
  map.mem = target;
  map.address = (u64) output;
  REQUIRE(!__sys_map(space, &map), 137);
  for (i = 0; i < COPY_BYTES; i++)
    input[i] = (u8) (i * 37 + 11);
  REQUIRE(!__sys_copy((void *) output, (const void *) input, COPY_BYTES), 138);
  for (i = 0; i < COPY_BYTES; i++)
    REQUIRE(output[i] == input[i], 139);
  REQUIRE(__sys_copy((void *) 0xa00000, (const void *) input, 32) == -ERR_INVALID, 140);
  REQUIRE(__sys_copy((void *) (input + 1), (const void *) input, 32) == -ERR_INVALID, 141);
  REQUIRE(!__sys_copy((void *) 0xa00020, (const void *) input, 32), 142);
  REQUIRE(__sys_copy((void *) output, (const void *) input, COPY_BYTES + 1) == -ERR_INVALID, 143);
  REQUIRE(__sys_copy((void *) boot, (const void *) input, 16) == -ERR_FAULT, 144);
  REQUIRE(__sys_copy((void *) output, (const void *) 0, 16) == -ERR_FAULT, 145);
  REQUIRE(__sys_copy((void *) output, (const void *) (~(u64) 0 - 7), 16) == -ERR_FAULT, 146);
  for (i = 0; i < 8; i++)
    output[COPY_BYTES - 8 + i] = 0x5a;
  REQUIRE(__sys_copy((void *) (output + COPY_BYTES - 8), (const void *) input, 16) == -ERR_FAULT,
          147);
  for (i = 0; i < 8; i++)
    REQUIRE(output[COPY_BYTES - 8 + i] == 0x5a, 148);
  args.bytes = PAGE_SIZE;
  args.kind = ALLOC_CODE;
  REQUIRE(!__sys_alloc(allocator, &args, &code) && !__sys_seal(code), 149);
  map.mem = code;
  map.length = PAGE_SIZE;
  map.prot = MAP_READ | MAP_EXEC;
  map.address = 0xc00000;
  REQUIRE(!__sys_map(space, &map), 150);
  REQUIRE(__sys_copy((void *) 0xc00000, (const void *) input, 16) == -ERR_FAULT, 151);
  REQUIRE(!__sys_copy((void *) output, (const void *) 0xc00000, 16), 152);
  for (i = 0; i < 16; i++)
    REQUIRE(!output[i], 153);
  REQUIRE(!__sys_unmap(space, 0xc00000) && !__sys_free(code), 154);
  REQUIRE(!__sys_unmap(space, 0xa00000) && !__sys_unmap(space, (u64) input), 155);
  REQUIRE(__sys_copy((void *) output, (const void *) input, 16) == -ERR_FAULT, 156);
  REQUIRE(!__sys_copy(NULL, NULL, 0), 157);
  REQUIRE(!__sys_unmap(space, (u64) output) && !__sys_free(source) && !__sys_free(target), 158);
}
static u64
accountused(Handle authority, u64 *state)
{
  Queryrec record;
  u64      count, next;
  REQUIRE(!__sys_query(authority, QUERY_ACCT, 0, &record, 1, &count, &next) && !next, 159);
  if (!count) {
    *state = 0;
    return 0;
  }
  REQUIRE(count == 1, 160);
  *state = record.data[5];
  return record.data[3];
}
static void
sharedclient(const Bootview *view, Handle peer)
{
  Handle allocator = bootresource(view, BOOT_SELF_ALLOC),
         space = bootresource(view, BOOT_SELF_SPACE);
  Handle        management = bootresource(view, 258), memory, remote;
  Allocarg      arg;
  Maparg        map;
  Msg           words;
  Ipcmsg        result;
  volatile u64 *alias = (volatile u64 *) 0xe00000;
  u64           state, baseline = accountused(0, &state);
  arg.bytes = 3 * PAGE_SIZE;
  arg.kind = ALLOC_SHARED;
  arg.align = arg.minaddr = arg.maxaddr = arg.reserved = 0;
  REQUIRE(!__sys_alloc(allocator, &arg, &memory), 161);
  REQUIRE(__sys_seal(memory) == -ERR_DENIED, 162);
  REQUIRE(__sys_grant(management, memory, MAP_READ | MAP_WRITE | MAP_EXEC, 0, PAGE_SIZE, 1,
                      &remote) == -ERR_DENIED &&
              !remote,
          163);
  REQUIRE(!__sys_grant(management, memory, MAP_READ | MAP_WRITE, PAGE_SIZE, PAGE_SIZE, 1, &remote),
          164);
  map.mem = memory;
  map.offset = 0;
  map.address = (u64) alias;
  map.length = 3 * PAGE_SIZE;
  map.prot = MAP_READ | MAP_WRITE;
  map.reserved = 0;
  REQUIRE(!__sys_map(space, &map), 165);
  alias[PAGE_SIZE / 8] = UINT64_C(0x123456789abcdef0);
  words.words[0] = 100;
  words.words[1] = (u64) remote;
  words.words[2] = 0;
  words.words[3] = 0;
  REQUIRE(!ipc_call(peer, &words, 0, &result) && result.words[0] == 101, 166);
  REQUIRE(alias[PAGE_SIZE / 8] == UINT64_C(0xfedcba9876543210), 167);
  REQUIRE(!__sys_revoke(management, remote), 168);
  REQUIRE(!__sys_unmap(space, (u64) alias) && !__sys_free(memory), 169);
  REQUIRE(accountused(0, &state) == baseline + 3 && state == 1, 170);
  words.words[0] = 101;
  REQUIRE(!ipc_call(peer, &words, 0, &result) && result.words[0] == 102, 171);
}
static void
sharedserver(const Bootview *view)
{
  Handle        space = bootresource(view, BOOT_SELF_SPACE), memory;
  Maparg        map;
  Msg           words;
  Ipcmsg        received;
  volatile u64 *alias = (volatile u64 *) 0xd00000;
  REQUIRE(!ipc_recv(0, 0, &received) && received.kind == IPC_CALL && received.words[0] == 100, 172);
  memory = (Handle) received.words[1];
  map.mem = memory;
  map.offset = 0;
  map.address = (u64) alias;
  map.length = PAGE_SIZE;
  map.prot = MAP_READ | MAP_WRITE;
  map.reserved = 0;
  REQUIRE(__sys_map(space, &map) == -ERR_DENIED, 173);
  map.offset = PAGE_SIZE;
  map.prot = MAP_READ | MAP_EXEC;
  REQUIRE(__sys_map(space, &map) == -ERR_DENIED, 174);
  map.prot = MAP_READ | MAP_WRITE;
  REQUIRE(!__sys_map(space, &map) && alias[0] == UINT64_C(0x123456789abcdef0), 175);
  alias[0] = UINT64_C(0xfedcba9876543210);
  words.words[0] = 101;
  words.words[1] = words.words[2] = words.words[3] = 0;
  REQUIRE(!ipc_reply(received.request, &words), 176);
  REQUIRE(!ipc_recv(0, 0, &received) && received.kind == IPC_CALL && received.words[0] == 101, 177);
  map.address = 0xf00000;
  REQUIRE(__sys_map(space, &map) == -ERR_BAD_HANDLE, 178);
  REQUIRE(alias[0] == UINT64_C(0xfedcba9876543210), 179);
  words.words[0] = 102;
  REQUIRE(!ipc_reply(received.request, &words), 180);
}
static void
sharedfinish(const Bootview *view)
{
  u64    state, used;
  u32    turns = 0;
  Handle management = bootresource(view, 258), space = bootresource(view, BOOT_SELF_SPACE);
  do {
    used = accountused(management, &state);
    REQUIRE(++turns < 64, 181);
    if (state == 2 && used == 3)
      break;
    REQUIRE(!__sys_yield(), 182);
  } while (1);
  REQUIRE(*(volatile u64 *) 0xd00000 == UINT64_C(0xfedcba9876543210), 183);
  REQUIRE(!__sys_unmap(space, 0xd00000), 184);
  REQUIRE(accountused(management, &state) == 0 && !state, 185);
}
NORETURN void
_start(const Boot *boot)
{
  Bootview view;
  Ipcmsg   result;
  Msg      words;
  Queryrec records[16];
  Handle   pid, parent, peer, rpc;
  u64      image, written, next, state;
  u32      i, client;
  REQUIRE(!bootcheck(boot, boot->bytes, &view), 109);
  REQUIRE(boot->argc == 1 && boot->envc == 1 && bootarg(&view, 0, 1)[0] == 'S', 110);
  REQUIRE(!bootarg(&view, 1, 0) && !bootarg(&view, 0, 2), 111);
  client = bootarg(&view, 0, 0)[0] == 'c';
  REQUIRE(!__sys_getid(&pid, &image, &parent) && pid > 1 && image == 1 && parent == 1, 112);
  REQUIRE(initialized == UINT64_C(0x123456789abcdef0) && readonly == UINT64_C(0xfedcba9876543210),
          113);
  for (i = 0; i < ARRAY_SIZE(bss); i++) {
    REQUIRE(!bss[i], 114);
    bss[i] = i + 1;
  }
  peer = bootresource(&view, 256);
  REQUIRE(peer > 0 && !bootresource(&view, BOOT_SYSTEM), 115);
  REQUIRE(!__sys_query(0, QUERY_RESOURCE, 0, records, 16, &written, &next) && !next, 116);
  for (i = 0; i < written; i++)
    if ((Handle) records[i].data[0] == peer)
      break;
  REQUIRE(i < written && records[i].data[1] == 5 && records[i].data[6] != (u64) pid &&
              records[i].data[7] == 1,
          117);
  memory(bootresource(&view, BOOT_SELF_ALLOC), bootresource(&view, BOOT_SELF_SPACE));
  copycheck(bootresource(&view, BOOT_SELF_ALLOC), bootresource(&view, BOOT_SELF_SPACE), boot);
  words.words[0] = UINT64_C(0x0123456789abcdef);
  words.words[1] = UINT64_C(0xfedcba9876543210);
  words.words[2] = UINT64_C(0xaaaaaaaa55555555);
  words.words[3] = UINT64_C(0x8000000000000042);
  if (client)
    sharedclient(&view, peer);
  else
    sharedserver(&view);
  if (client) {
    REQUIRE(!ipc_call(peer, &words, CALL_ASYNC, &result) && result.request > 0, 118);
    checkempty(&result, 1);
    rpc = result.request;
    REQUIRE(!ipc_rqwait(rpc, 0, &result), 119);
    checkempty(&result, 0);
    REQUIRE(result.words[0] == words.words[3] && result.words[1] == words.words[2] &&
                result.words[2] == words.words[1] && result.words[3] == words.words[0],
            120);
    REQUIRE(!ipc_rqack(rpc, ACK_RESULT | ACK_RELEASE), 121);
    REQUIRE(ipc_rqpoll(rpc, 0, &result) == -ERR_STALE && !result.request, 122);
    checkempty(&result, 0);
    REQUIRE(!ipc_call(peer, &words, 0, &result), 123);
    REQUIRE(result.words[3] == words.words[0] && !result.request && !result.image, 124);
    REQUIRE(ipc_send(peer, &words) == -ERR_BAD_HANDLE, 133);
    REQUIRE(!ipc_send(bootresource(&view, 257), &words), 125);
  } else {
    for (i = 0; i < 3; i++) {
      Msg response;
      REQUIRE(!ipc_recv(0, 0, &result), 126);
      REQUIRE(result.kind == (i == 2 ? IPC_SEND : IPC_CALL) && result.source != pid &&
                  result.source > 1 && result.image == 1 &&
                  (i == 2 ? !result.request : result.request > 0),
              127);
      REQUIRE(result.words[0] == words.words[0] && result.words[1] == words.words[1] &&
                  result.words[2] == words.words[2] && result.words[3] == words.words[3],
              128);
      if (i == 2)
        break;
      REQUIRE(!ipc_rqcheck(result.request, &state) && state == 1, 129);
      response.words[0] = result.words[3];
      response.words[1] = result.words[2];
      response.words[2] = result.words[1];
      response.words[3] = result.words[0];
      if (!i)
        REQUIRE(!ipc_defer(result.request) && !__sys_yield(), 130);
      REQUIRE(!ipc_reply(result.request, &response), 131);
    }
  }
  if (!client)
    sharedfinish(&view);
  REQUIRE(bss[256] == 257 && !__sys_yield(), 132);
  __sys_exit(42);
}
