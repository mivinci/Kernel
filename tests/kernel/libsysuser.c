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
  words.words[0] = UINT64_C(0x0123456789abcdef);
  words.words[1] = UINT64_C(0xfedcba9876543210);
  words.words[2] = UINT64_C(0xaaaaaaaa55555555);
  words.words[3] = UINT64_C(0x8000000000000042);
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
  REQUIRE(bss[256] == 257 && !__sys_yield(), 132);
  __sys_exit(42);
}
