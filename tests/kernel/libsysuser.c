#include <sys/boot.h>
#include <sys/channel.h>
#include "private/syscall.h"
#include "private/conn.h"
#include <abi/syscall.h>
static volatile u64       bss[257];
static volatile u64       initialized = UINT64_C(0x123456789abcdef0);
static const volatile u64 readonly = UINT64_C(0xfedcba9876543210);
static Conncache          connection_directory;
static Conntag            connection_tag;
static Connpeer           connection_peer;
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
static Handle pressure[128];
static void
receiveclient(const Bootview *view, Handle peer)
{
  Handle   allocator = bootresource(view, BOOT_SELF_ALLOC), rpc, extra, unused;
  Allocarg arg;
  Msg      words;
  Ipcmsg   result;
  u32      count = 0;
  isize    error;
  arg.bytes = PAGE_SIZE;
  arg.kind = ALLOC_DATA;
  arg.align = arg.minaddr = arg.maxaddr = arg.reserved = 0;
  words.words[0] = 110;
  words.words[1] = words.words[2] = words.words[3] = 0;
  REQUIRE(!ipc_call(peer, &words, CALL_ASYNC | CALL_RECV_MEM, &result) && result.request > 0, 186);
  rpc = result.request;
  while (!(error = __sys_alloc(allocator, &arg, &pressure[count]))) {
    REQUIRE(++count < ARRAY_SIZE(pressure), 187);
  }
  REQUIRE(error == -ERR_CAPACITY && count > 1, 188);
  REQUIRE(!ipc_rqcancel(rpc) && !ipc_rqack(rpc, ACK_RESULT | ACK_RELEASE), 189);
  REQUIRE(__sys_alloc(allocator, &arg, &unused) == -ERR_CAPACITY && !unused, 190);
  words.words[0] = 111;
  REQUIRE(!ipc_call(peer, &words, CALL_ASYNC | CALL_RECV_CALL, &result) && result.request > 0, 191);
  rpc = result.request;
  words.words[0] = 112;
  REQUIRE(!ipc_call(peer, &words, CALL_ASYNC | CALL_RECV_MEM, &result) && result.request > 0, 192);
  extra = result.request;
  REQUIRE(ipc_call(peer, &words, CALL_ASYNC | CALL_RECV_MEM, &result) == -ERR_CAPACITY &&
              !result.request,
          193);
  REQUIRE(!ipc_rqcancel(extra) && !ipc_rqack(extra, ACK_RESULT | ACK_RELEASE), 194);
  REQUIRE(!ipc_recv(0, 0, &result) && result.kind == IPC_SEND && result.words[0] == 111, 195);
  REQUIRE(!ipc_rqcancel(rpc), 196);
  REQUIRE(!ipc_call(peer, &words, CALL_ASYNC | CALL_RECV_MEM, &result) && result.request > 0, 197);
  extra = result.request;
  REQUIRE(ipc_call(peer, &words, CALL_ASYNC | CALL_RECV_MEM, &result) == -ERR_CAPACITY &&
              !result.request,
          198);
  REQUIRE(!ipc_rqcancel(extra) && !ipc_rqack(extra, ACK_RESULT | ACK_RELEASE), 199);
  REQUIRE(__sys_alloc(allocator, &arg, &unused) == -ERR_CAPACITY && !unused, 200);
  while (count)
    REQUIRE(!__sys_free(pressure[--count]), 201);
  REQUIRE(!ipc_rqwait(rpc, 0, &result) && result.words[0] == 113 && !result.kind, 202);
  REQUIRE(!ipc_rqpoll(rpc, 0, &result) && result.words[0] == 113 && !result.kind, 203);
  REQUIRE(!ipc_rqack(rpc, ACK_RESULT | ACK_RELEASE), 204);
}
static void
receiveserver(const Bootview *view)
{
  Ipcmsg received;
  Msg    words;
  u64    state;
  REQUIRE(!ipc_recv(0, 0, &received) && received.kind == IPC_CALL && received.words[0] == 111, 205);
  REQUIRE(!ipc_defer(received.request), 206);
  words.words[0] = 111;
  words.words[1] = words.words[2] = words.words[3] = 0;
  REQUIRE(!ipc_send(bootresource(view, 257), &words), 207);
  REQUIRE(!ipc_rqcheck(received.request, &state) && state == 3, 208);
  words.words[0] = 113;
  REQUIRE(!ipc_reply(received.request, &words), 209);
}
volatile u64 resource_trace[16];
static void
replyerror(Handle request, u64 address, isize expected, u32 extra)
{
  u64    args[7] = {(u64) request, address, 0, 0, 0, REPLY_RESOURCE, 0};
  Sysret result;
  u64    state;
  u32    i;
  if (extra)
    args[extra] = extra == 5 ? 2 : 1;
  REQUIRE(__sys_enter(SYS_REPLY, args, &result) == expected, 240);
  for (i = 0; i < 8; i++)
    REQUIRE(!((u64 *) &result)[i], 241);
  REQUIRE(!ipc_rqcheck(request, &state) &&
              (state == RQ_CHECK_LIVE || state == (RQ_CHECK_LIVE | RQ_CHECK_CANCEL)),
          242);
  resource_trace[11]++;
}

static void
publishresource(const Bootview *view, Handle request, const Resreply *reply, u64 stage)
{
  u32      i;
  u64      state;
  Resreply bad;
  resource_trace[1] = (u64) request;
  resource_trace[2] = (u64) reply->source;
  resource_trace[3] = reply->rights;
  resource_trace[4] = reply->offset;
  resource_trace[5] = reply->length;
  for (i = 0; i < 3; i++)
    resource_trace[6 + i] = reply->business[i];
  resource_trace[0] = stage;
  replyerror(request, (u64) reply, -ERR_INVALID, 2);
  replyerror(request, (u64) reply, -ERR_INVALID, 3);
  replyerror(request, (u64) reply, -ERR_INVALID, 4);
  replyerror(request, (u64) reply, -ERR_INVALID, 5);
  replyerror(request, (u64) reply, -ERR_INVALID, 6);
  replyerror(request, 0, -ERR_INVALID, 0);
  replyerror(request, (u64) reply + 1, -ERR_INVALID, 0);
  replyerror(request, BOOT_USER_HI - 8, -ERR_INVALID, 0);
  replyerror(request, 0x1200000, -ERR_FAULT, 0);
  REQUIRE(!__sys_copy(&bad, reply, sizeof(bad)), 159);
  bad.source = 0;
  replyerror(request, (u64) &bad, -ERR_BAD_HANDLE, 0);
  REQUIRE(!__sys_copy(&bad, reply, sizeof(bad)), 159);
  bad.rights |= UINT64_C(1) << 63;
  replyerror(request, (u64) &bad, -ERR_DENIED, 0);
  REQUIRE(!__sys_copy(&bad, reply, sizeof(bad)), 159);
  bad.offset = 1;
  replyerror(request, (u64) &bad, -ERR_INVALID, 0);
  if (stage == 1) {
    Allocarg alloc;
    Maparg   map;
    Handle   descriptor;
    Handle   space = bootresource(view, BOOT_SELF_SPACE);
    u64      address = 0x1200000 + PAGE_SIZE - 24;
    alloc.bytes = 2 * PAGE_SIZE;
    alloc.kind = ALLOC_DATA;
    alloc.align = alloc.minaddr = alloc.maxaddr = alloc.reserved = 0;
    REQUIRE(!__sys_alloc(bootresource(view, BOOT_SELF_ALLOC), &alloc, &descriptor), 243);
    map.mem = descriptor;
    map.offset = 0;
    map.address = 0x1200000;
    map.length = PAGE_SIZE;
    map.prot = MAP_READ | MAP_WRITE;
    map.reserved = 0;
    REQUIRE(!__sys_map(space, &map) && !__sys_copy((void *) address, reply, 24), 244);
    replyerror(request, address, -ERR_FAULT, 0);
    map.offset = PAGE_SIZE;
    map.address += PAGE_SIZE;
    REQUIRE(!__sys_map(space, &map) &&
                !__sys_copy((void *) map.address, (const u8 *) reply + 24, 32),
            245);
    REQUIRE(!__sys_unmap(space, map.address), 246);
    map.prot = MAP_READ;
    REQUIRE(!__sys_map(space, &map) && !ipc_reply_resource(request, (const Resreply *) address),
            247);
    REQUIRE(!__sys_unmap(space, 0x1200000) && !__sys_unmap(space, 0x1201000) &&
                !__sys_free(descriptor),
            248);
  } else
    REQUIRE(!ipc_reply_resource(request, reply), 210);
  resource_trace[10] = stage;
  REQUIRE(ipc_rqcheck(request, &state) == -ERR_BAD_HANDLE, 211);
  REQUIRE(ipc_reply_resource(request, reply) == -ERR_BAD_HANDLE, 239);
}
static void
typedserver(const Bootview *view)
{
  Ipcmsg   received;
  Allocarg arg;
  Maparg   map;
  Resreply reply;
  Msg      words;
  Handle   memory, request, space = bootresource(view, BOOT_SELF_SPACE);
  u64      state;
  REQUIRE(!ipc_recv(0, 0, &received) && received.kind == IPC_CALL && received.words[0] == 200, 212);
  REQUIRE(!ipc_defer(received.request), 213);
  request = received.request;
  words.words[0] = 206;
  words.words[1] = words.words[2] = words.words[3] = 0;
  REQUIRE(!ipc_send(bootresource(view, 257), &words), 157);
  REQUIRE(!ipc_recv(0, 0, &received) && received.kind == IPC_SEND && received.words[0] == 207, 158);
  REQUIRE(!ipc_rqcheck(request, &state) && state == (RQ_CHECK_LIVE | RQ_CHECK_CANCEL), 156);
  resource_trace[14] = 1;
  arg.bytes = 3 * PAGE_SIZE;
  arg.kind = ALLOC_SHARED;
  arg.align = arg.minaddr = arg.maxaddr = arg.reserved = 0;
  REQUIRE(!__sys_alloc(bootresource(view, BOOT_SELF_ALLOC), &arg, &memory), 214);
  map.mem = memory;
  map.offset = 0;
  map.address = 0x1100000;
  map.length = arg.bytes;
  map.prot = MAP_READ | MAP_WRITE;
  map.reserved = 0;
  REQUIRE(!__sys_map(space, &map), 215);
  *(volatile u64 *) (0x1100000 + PAGE_SIZE) = UINT64_C(0x123456789abcdef0);
  reply.source = memory;
  reply.rights = MAP_READ | MAP_WRITE;
  reply.offset = PAGE_SIZE;
  reply.length = PAGE_SIZE;
  reply.business[0] = 17;
  reply.business[1] = 23;
  reply.business[2] = UINT64_C(0x8000000000000042);
  publishresource(view, request, &reply, 1);
  REQUIRE(!__sys_unmap(space, 0x1100000) && !__sys_free(memory), 216);
  REQUIRE(!ipc_recv(0, 0, &received) && received.kind == IPC_CALL && received.words[0] == 201, 217);
  REQUIRE(!ipc_defer(received.request), 218);
  reply.source = bootresource(view, 259);
  reply.rights = 1;
  reply.offset = reply.length = 0;
  publishresource(view, received.request, &reply, 2);
  REQUIRE(!ipc_recv(0, 0, &received) && received.kind == IPC_CALL && received.words[0] == 202, 219);
  words.words[0] = 303;
  words.words[1] = words.words[2] = words.words[3] = 0;
  REQUIRE(!ipc_reply(received.request, &words), 220);
  REQUIRE(!ipc_recv(0, 0, &received) && received.kind == IPC_CALL && received.words[0] == 203, 249);
  words.words[0] = (u64) &reply;
  words.words[1] = 57;
  words.words[2] = 59;
  words.words[3] = 61;
  REQUIRE(ipc_reply_recv(received.request, &words, 0, RECV_NONBLOCK, &received) == -ERR_WOULD_BLOCK,
          250);
}
static void
checkreceived(Handle handle, u64 subtype, u64 offset, u64 length)
{
  Queryrec rows[4];
  u64      cursor = 0, count, next, i, found = 0;
  do {
    REQUIRE(!__sys_query(0, QUERY_RESOURCE, cursor, rows, 4, &count, &next), 221);
    for (i = 0; i < count; i++)
      if ((Handle) rows[i].data[0] == handle) {
        REQUIRE(rows[i].data[1] == subtype && rows[i].data[2] == (subtype == 4 ? 3U : 1U) &&
                    rows[i].data[3] == offset && rows[i].data[4] == length && rows[i].data[5] == 1,
                222);
        found++;
      }
    cursor = next;
  } while (cursor);
  REQUIRE(found == 1, 223);
}
static void
typedclient(const Bootview *view, Handle peer)
{
  Msg    words;
  Ipcmsg result;
  Handle rpc, memory, backend, space = bootresource(view, BOOT_SELF_SPACE), denied;
  Maparg map;
  words.words[0] = 200;
  words.words[1] = words.words[2] = words.words[3] = 0;
  REQUIRE(!ipc_call(peer, &words, CALL_ASYNC | CALL_RECV_MEM, &result) && result.request > 0, 224);
  rpc = result.request;
  REQUIRE(!ipc_recv(0, 0, &result) && result.kind == IPC_SEND && result.words[0] == 206, 155);
  REQUIRE(!ipc_rqcancel(rpc), 154);
  words.words[0] = 207;
  REQUIRE(!ipc_send(bootresource(view, 257), &words), 153);
  REQUIRE(!ipc_rqwait(rpc, 0, &result) && result.kind == IPC_RESOURCE_REPLY && !result.source &&
              !result.request && !result.image,
          225);
  REQUIRE(result.words[1] == 17 && result.words[2] == 23 &&
              result.words[3] == UINT64_C(0x8000000000000042),
          226);
  memory = (Handle) result.words[0];
  checkreceived(memory, 4, PAGE_SIZE, PAGE_SIZE);
  REQUIRE(!ipc_rqcancel(rpc) && !ipc_rqpoll(rpc, 0, &result) && result.kind == IPC_RESOURCE_REPLY &&
              (Handle) result.words[0] == memory,
          227);
  REQUIRE(!ipc_rqack(rpc, ACK_RESULT | ACK_RELEASE), 228);
  map.mem = memory;
  map.offset = PAGE_SIZE;
  map.address = 0x1000000;
  map.length = PAGE_SIZE;
  map.prot = MAP_READ | MAP_WRITE;
  map.reserved = 0;
  REQUIRE(!__sys_map(space, &map) && *(volatile u64 *) 0x1000000 == UINT64_C(0x123456789abcdef0),
          229);
  REQUIRE(__sys_grant(bootresource(view, 258), memory, 1, PAGE_SIZE, PAGE_SIZE, 1, &denied) ==
                  -ERR_DENIED &&
              !denied,
          230);
  REQUIRE(!__sys_unmap(space, 0x1000000) && !__sys_free(memory), 231);
  words.words[0] = 201;
  REQUIRE(!ipc_call(peer, &words, CALL_RECV_CALL, &result) && result.kind == IPC_RESOURCE_REPLY &&
              !result.request && !result.source && !result.image,
          232);
  backend = (Handle) result.words[0];
  checkreceived(backend, 5, 0, 0);
  {
    Connpeer alias;
    Conntag  tag;
    REQUIRE(backend != peer && !sysconnpeer(backend, &alias) && alias.pid == connection_peer.pid &&
                alias.image == connection_peer.image &&
                !sysconnbind(&connection_directory, alias, &tag) &&
                tag.index == connection_tag.index && tag.generation == connection_tag.generation,
            155);
  }
  words.words[0] = 202;
  REQUIRE(!ipc_call(backend, &words, 0, &result) && !result.kind && result.words[0] == 303, 236);
  REQUIRE(!__sys_free(backend), 237);
  {
    Connpeer old = connection_peer;
    REQUIRE(sysconnpeer(backend, &old) == -ERR_STALE && old.pid == connection_peer.pid &&
                old.image == connection_peer.image,
            154);
  }
  REQUIRE(ipc_call(backend, &words, 0, &result) == -ERR_BAD_HANDLE, 238);
  words.words[0] = 203;
  REQUIRE(!ipc_call(peer, &words, CALL_RECV_MEM, &result) && !result.kind && result.words[0] &&
              result.words[1] == 57 && result.words[2] == 59 && result.words[3] == 61,
          251);
}
static Handle
latebegin(const Bootview *view, Handle peer, u32 client)
{
  Msg    words;
  Ipcmsg message;
  Handle request;
  words.words[0] = client ? 204 : 205;
  words.words[1] = words.words[2] = words.words[3] = 0;
  if (client) {
    REQUIRE(!ipc_call(peer, &words, CALL_ASYNC | CALL_RECV_MEM, &message) && message.request, 252);
    REQUIRE(!ipc_recv(0, 0, &message) && message.kind == IPC_SEND && message.words[0] == 205, 253);
    return 0;
  }
  REQUIRE(!ipc_recv(0, 0, &message) && message.kind == IPC_CALL && message.words[0] == 204, 254);
  request = message.request;
  REQUIRE(!ipc_defer(request) && !ipc_send(bootresource(view, 257), &words), 255);
  return request;
}

static void
latefinish(const Bootview *view, Handle request)
{
  Allocarg alloc;
  Resreply reply;
  Handle   memory;
  u64      state;
  alloc.bytes = PAGE_SIZE;
  alloc.kind = ALLOC_SHARED;
  alloc.align = alloc.minaddr = alloc.maxaddr = alloc.reserved = 0;
  REQUIRE(!__sys_alloc(bootresource(view, BOOT_SELF_ALLOC), &alloc, &memory), 160);
  reply.source = memory;
  reply.rights = MAP_READ;
  reply.offset = 0;
  reply.length = PAGE_SIZE;
  reply.business[0] = reply.business[1] = reply.business[2] = 0;
  REQUIRE(!ipc_rqcheck(request, &state) && state == RQ_CHECK_DRAINING, 161);
  REQUIRE(!ipc_reply_resource(request, &reply) && !__sys_free(memory), 162);
  REQUIRE(ipc_rqcheck(request, &state) == -ERR_BAD_HANDLE, 163);
  resource_trace[12] = 1;
}
NORETURN void
_start(const Boot *boot)
{
  Bootview view;
  Ipcmsg   result;
  Msg      words;
  Queryrec records[16];
  Handle   pid, parent, peer, rpc, late;
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
  {
    Connpeer  self, unchanged;
    Connclaim claim, nested;
    self.pid = pid;
    self.image = image;
    REQUIRE(!sysconnpeer(peer, &connection_peer) &&
                connection_peer.pid == (Handle) records[i].data[6] &&
                connection_peer.image == records[i].data[7] &&
                !sysconninit(&connection_directory, self) &&
                !sysconnbind(&connection_directory, connection_peer, &connection_tag) &&
                !sysconnbegin(&connection_directory, connection_tag, 101, &claim) &&
                sysconnbegin(&connection_directory, connection_tag, 202, &nested) == -ERR_BUSY &&
                !sysconnrollback(&connection_directory, claim) &&
                !sysconncheck(&connection_directory, self),
            152);
    unchanged = connection_peer;
    REQUIRE(sysconnpeer(bootresource(&view, BOOT_SELF_SPACE), &unchanged) == -ERR_BAD_HANDLE &&
                unchanged.pid == connection_peer.pid && unchanged.image == connection_peer.image,
            151);
    resource_trace[15] = 1;
  }
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
  if (client)
    receiveclient(&view, peer);
  else
    receiveserver(&view);
  if (client)
    typedclient(&view, peer);
  else
    typedserver(&view);
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
      if (!i) {
        Resreply refused;
        refused.source = bootresource(&view, 259);
        refused.rights = 1;
        refused.offset = refused.length = 0;
        refused.business[0] = refused.business[1] = refused.business[2] = 0;
        REQUIRE(ipc_reply_resource(result.request, &refused) == -ERR_DENIED, 152);
        REQUIRE(!ipc_rqcheck(result.request, &state) && state == RQ_CHECK_LIVE, 151);
        resource_trace[13] = 1;
      }
      response.words[0] = result.words[3];
      response.words[1] = result.words[2];
      response.words[2] = result.words[1];
      response.words[3] = result.words[0];
      if (!i)
        REQUIRE(!ipc_defer(result.request) && !__sys_yield(), 130);
      REQUIRE(!ipc_reply(result.request, &response), 131);
    }
  }
  late = latebegin(&view, peer, client);
  if (!client) {
    sharedfinish(&view);
    latefinish(&view, late);
  }
  REQUIRE(bss[256] == 257 && !__sys_yield(), 132);
  __sys_exit(42);
}
