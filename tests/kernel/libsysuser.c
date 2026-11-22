#include <sys/boot.h>
#include <sys/channel.h>
#include "private/syscall.h"
#include "private/conn.h"
#include "private/bootstrap.h"
#include "private/connection.h"
#include "private/operation.h"
#include "private/fileref.h"
#include "private/fd.h"
#include "private/close.h"
#include "private/contextipc.h"
#include "private/fork.h"
#include <proto/file.h>
#include <abi/syscall.h>
#include <abi/time.h>
static volatile u64       bss[257];
static volatile u64       initialized = UINT64_C(0x123456789abcdef0);
static const volatile u64 readonly = UINT64_C(0xfedcba9876543210);
static Connwork           connection_work;
#define connection_directory (*(Conncache *) (void *) connection_work.reserved)
static Conntag      connection_tag;
static Connpeer     connection_peer;
static Bootstrap    bootstrap_record;
static Filepool     file_registry;
static Fdtable      file_descriptors;
static Rbank        runtime_contexts;
static Rkey         runtime_key;
static volatile u64 input_source[4] = {0, 4, 0, 0};
static char         argument_word[] = "original";
static char         argument_env[] = "K=one";
static const char  *argument_envp[] = {argument_env, NULL};
static Handle       argument_space, argument_memory;
static u64          argument_vector;
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
volatile u64  resource_trace[58];
static u32    readforkchild;
static Handle readforkrpc;
static void
readfork(void)
{
  u64             fix[FIX_BASE_BYTES / sizeof(u64)];
  Forkfix         finish = {0};
  Forkret         forked;
  Elfwork         work;
  Fixview         view;
  const Fixlayer *layers;
  Connpeer        parent = runtime_contexts.self, child;
  Handle          ppid, published;
  Sysret          result;
  u64             args[7] = {0}, mask, ignored;
  u32             i;
  readforkrpc = runtime_contexts.contexts[0].route.rpc;
  REQUIRE(handlekind(readforkrpc) == HANDLE_RPC &&
              !__sys_sigmask(SIGNAL_BLOCK, SIGNAL_SUPPORTED, &mask) &&
              !__sys_forkprep((Handle) resource_trace[56], 0, &forked),
          32);
  if (!forked.construct) {
    Connwork *aliases[] = {&connection_work};
    u64       inherited = connection_work.state.map.address;
    u32       sample;
    REQUIRE(!__sys_getid(&child.pid, &child.image, &ppid) && ppid == parent.pid &&
                !sysfixchild(&forked, sizeof(fix), parent.pid, parent.image, &view) &&
                !sysfixlayers(&view, &layers) &&
                !sysctxfixwaitcheck(&runtime_contexts, parent, child, layers) && inherited &&
                !__sys_copy(&sample, (const void *) inherited, sizeof(sample)) &&
                !sysconnforkunmaps(aliases, ARRAY_SIZE(aliases), parent, child,
                                   connection_work.state.space) &&
                __sys_copy(&sample, (const void *) inherited, sizeof(sample)) == -ERR_FAULT &&
                !sysconnforkunmap(&connection_work, parent, child, connection_work.state.space) &&
                !sysctxfixwait(&runtime_contexts, parent, child, layers),
            33);
    readforkchild = 1;
  } else {
    for (i = 0; i < sizeof(work); i++)
      ((u8 *) &work)[i] = 0;
    work.view = forked.construct;
    work.pid = forked.pid;
    work.parent_space = connection_work.state.space;
    work.scratch = 0x31000000;
    work.scratch_bytes = PAGE_SIZE;
    finish.address = 0x32000000;
    finish.buffer = fix;
    finish.capacity = sizeof(fix);
    published = sysforkfinish(&work, &runtime_contexts, parent, &finish);
    REQUIRE(published == forked.pid && !work.view && !work.memory && !work.mapped, 34);
    args[0] = (u64) published;
    REQUIRE(!__sys_enter(SYS_WAIT, args, &result) && result.values[0] == (u64) published &&
                result.values[1] == WAIT_EXITED && result.values[2] == 43 &&
                connection_work.state.map.mapped && connection_work.state.map.verified &&
                !__sys_copy(&i, (const void *) connection_work.state.map.address, sizeof(i)),
            35);
    resource_trace[57]++;
  }
  REQUIRE(!__sys_sigmask(SIGNAL_SETMASK, mask, &ignored), 36);
}
static void
referenceprepare(Connpeer self, Conntag connection, u64 *file)
{
  Filestage stages[128], extra, stale;
  Filevalue value;
  Fileref   view;
  u64       unused = 0;
  u32       i;
  for (i = 0; i < sizeof(stages); i++)
    ((u8 *) stages)[i] = 0;
  for (i = 0; i < sizeof(extra); i++)
    ((u8 *) &extra)[i] = 0;
  for (i = 0; i < ARRAY_SIZE(stages); i++)
    REQUIRE(!sysfilerefreserve(&file_registry, self, 2, &stages[i]), 27);
  stale = stages[0];
  REQUIRE(sysfilerefreserve(&file_registry, self, 1, &extra) == -ERR_CAPACITY && !extra.count &&
              sysfilerefview(&file_registry, self, stale.files[0], &view) == -ERR_STALE &&
              sysfilerefnew(&file_registry, self, connection, 55, 77, 0, &unused) ==
                  -ERR_CAPACITY &&
              !unused,
          28);
  for (i = 0; i < ARRAY_SIZE(stages); i++)
    REQUIRE(!sysfilerefabort(&file_registry, self, &stages[i]) && !stages[i].count, 29);
  REQUIRE(!sysfilerefreserve(&file_registry, self, 1, &extra) &&
              sysfilerefabort(&file_registry, self, &stale) == -ERR_STALE,
          30);
  *file = extra.files[0];
  value.connection = connection;
  value.session = 55;
  value.reference = 77;
  value.token = 0;
  REQUIRE(!sysfilerefcommit(&file_registry, self, &extra, &value, 0) && !extra.count &&
              !sysfilerefview(&file_registry, self, *file, &view) && view.aliases == 1 &&
              !view.pins && view.state == SYSFILE_REF_LIVE &&
              !sysfilerefcheck(&file_registry, self),
          31);
}
static int
argumenttext(const Argsnap *snapshot, u32 environment, u64 index, const char *expected, u64 bytes)
{
  const char *text;
  u64         length, i;
  if (sysargat(snapshot, environment, index, &text, &length) || length != bytes)
    return 0;
  for (i = 0; i < bytes; i++)
    if (text[i] != expected[i])
      return 0;
  return 1;
}
static u64
runtimenow(void)
{
  int64_t seconds;
  u64     ns;
  REQUIRE(!__sys_clock_gettime(CLOCK_MONOTONIC, &seconds, &ns) && seconds >= 0 &&
              (u64) seconds <= (UINT64_MAX - ns) / UINT64_C(1000000000),
          93);
  return (u64) seconds * UINT64_C(1000000000) + ns;
}
static void
runtimehandler(u64 signum)
{
  Rkey   inner;
  Sysret result;
  isize  status;
  u64    mask, ignored;
  Rctx  *outer = &runtime_contexts.contexts[0];
  u64    stage = resource_trace[16];
  REQUIRE(signum == SIGNAL_USR1, 108);
  if (stage == 7) {
    Argsnap snapshot;
    REQUIRE(outer->phase == RCTX_DONE && outer->route.kind == RCTX_ROUTE_NONE &&
                !sysctxargumentview(&runtime_contexts, connection_work.state.self, runtime_key,
                                    &snapshot) &&
                snapshot.argc == 2 && snapshot.envc == 1 &&
                argumenttext(&snapshot, 0, 0, "original", 9) &&
                argumenttext(&snapshot, 0, 1, "abc", 4) &&
                argumenttext(&snapshot, 1, 0, "K=one", 6),
            42);
    argument_word[0] = 'X';
    argument_env[2] = 'X';
    REQUIRE(!__sys_unmap(argument_space, 0x1700000) && !__sys_unmap(argument_space, 0x1800000) &&
                !__sys_free(argument_memory),
            41);
    argument_memory = 0;
    resource_trace[47] = 1;
  } else
    REQUIRE(outer->phase == RCTX_CLEANING && outer->route.delivered, 108);
  if (stage <= 4 || stage == 7 || stage == 8 || stage == 9)
    REQUIRE(sysctxfinish(&runtime_contexts, connection_work.state.self, runtime_key) == -ERR_BUSY,
            108);
  if (stage <= 2)
    REQUIRE(outer->route.bootstrap->state == (stage == 1 ? SYSBOOT_WAITING : SYSBOOT_RESULT) &&
                outer->route.bootstrap->rpc && (stage == 1 || outer->route.bootstrap->resource),
            107);
  if (stage <= 2) {
    const void *input;
    u64         bytes, length;
    REQUIRE(!sysctxinputview(&runtime_contexts, connection_work.state.self, runtime_key, &input,
                             &bytes) &&
                bytes == sizeof(input_source) &&
                !__sys_copy(&length, (const u8 *) input + sizeof(u64), sizeof(length)) &&
                length == 4,
            43);
    input_source[1] = 99;
    resource_trace[42] = 1;
  }
  if (stage > 2 && stage != 7) {
    Slotop *op = &outer->route.work->state.operations[outer->route.operation.lease.slot];
    REQUIRE(op->phase == (stage == 4 || stage == 8 || stage == 9 ? SYSOP_RESULT : SYSOP_WAITING) &&
                op->rpc && op->file,
            106);
    if (stage == 3 || stage == 5) {
      REQUIRE(op->flags & SYSOP_CANCEL_SENT && op->deadline, 100);
      if (stage == 3)
        resource_trace[22] = 1;
      else
        resource_trace[26] = 1;
    }
    if (stage == 4) {
      u32          value;
      const void  *output, *input;
      u64          bytes, input_bytes, length;
      volatile u8 *shared =
          (volatile u8 *) (uintptr_t) (outer->route.work->state.map.address + FS_DATA_OFFSET +
                                       outer->route.operation.lease.slot * FS_SLOT_BYTES);
      REQUIRE(outer->route.snapshot_saved && outer->route.snapshot_bytes == 4 &&
                  !sysctxsnapshotview(&runtime_contexts, connection_work.state.self, runtime_key,
                                      &output, &bytes) &&
                  bytes == sizeof(value) &&
                  !sysctxinputview(&runtime_contexts, connection_work.state.self, runtime_key,
                                   &input, &input_bytes) &&
                  input_bytes == sizeof(input_source) &&
                  (const u8 *) output == (const u8 *) input + input_bytes &&
                  !__sys_copy(&length, (const u8 *) input + sizeof(u64), sizeof(length)) &&
                  length == 4 && !__sys_copy(&value, output, sizeof(value)) &&
                  value == UINT32_C(0x74736574),
              99);
      shared[0] = shared[1] = shared[2] = shared[3] = 'X';
      resource_trace[23] = 1;
      resource_trace[44] = 1;
      readfork();
      if (readforkchild) {
        resource_trace[20]++;
        resource_trace[21] |= 1UL << stage;
        return;
      }
    }
    if (stage == 6) {
      REQUIRE(op->opcode == FS_CLOSE && !(op->flags & SYSOP_CANCEL_SENT) &&
                  op->deadline == resource_trace[33],
              66);
      resource_trace[32] = 1;
    }
    if (stage == 8 || stage == 9) {
      REQUIRE(op->opcode == FS_WRITE &&
                  (stage == 8 ? !op->words[0] && op->words[2] == 3
                              : (isize) op->words[0] == -ERR_DENIED && !op->words[2]) &&
                  !op->words[3] && outer->route.snapshot_saved && !outer->route.snapshot_bytes,
              30);
      readfork();
      if (readforkchild) {
        resource_trace[20]++;
        resource_trace[21] |= 1UL << stage;
        return;
      }
    }
  }
  REQUIRE(
      !__sys_sigmask(SIGNAL_BLOCK, SIGNAL_SUPPORTED, &mask) &&
          !sysctxprepare(&runtime_contexts, connection_work.state.self, 1, SYS_GETID, NULL,
                         &inner) &&
          !__sys_sigmask(SIGNAL_SETMASK, mask, &ignored) &&
          !sysctxinvoke(&runtime_contexts, connection_work.state.self, inner) &&
          !__sys_sigmask(SIGNAL_BLOCK, SIGNAL_SUPPORTED, &mask) &&
          !sysctxresult(&runtime_contexts, connection_work.state.self, inner, &status, &result) &&
          !status && result.values[0] == (u64) connection_work.state.self.pid &&
          !sysctxrearm(&runtime_contexts, connection_work.state.self, inner, SYS_GETID, NULL) &&
          !__sys_sigmask(SIGNAL_SETMASK, mask, &ignored) &&
          !sysctxinvoke(&runtime_contexts, connection_work.state.self, inner) &&
          !__sys_sigmask(SIGNAL_BLOCK, SIGNAL_SUPPORTED, &mask) &&
          !sysctxresult(&runtime_contexts, connection_work.state.self, inner, &status, &result) &&
          !status && result.values[0] == (u64) connection_work.state.self.pid &&
          !sysctxfinish(&runtime_contexts, connection_work.state.self, inner) &&
          !__sys_sigmask(SIGNAL_SETMASK, mask, &ignored),
      105);
  resource_trace[20]++;
  resource_trace[21] |= 1UL << stage;
}
static NORETURN void
runtimetrampoline(u64 signum, const Siginfo *info, void *trap, u64 function, u64 address, u64 flags)
{
  u64 mask, ignored, now;
  REQUIRE(signum == SIGNAL_USR1 &&
              ((!info->code && (resource_trace[16] <= 4 || resource_trace[16] == 7 ||
                                resource_trace[16] == 8 || resource_trace[16] == 9)) ||
               ((resource_trace[16] == 5 || resource_trace[16] == 6) &&
                info->code == SIGNAL_SOURCE_USER && info->pid == (u64) connection_peer.pid)) &&
              function == (u64) runtimehandler && !flags &&
              !__sys_sigmask(SIGNAL_BLOCK, SIGNAL_SUPPORTED, &mask),
          104);
  now = runtimenow();
  REQUIRE(!sysctxsignal(&runtime_contexts, connection_work.state.self, runtime_key, trap, now),
          104);
  if (resource_trace[16] == 5)
    resource_trace[30] = now;
  if (resource_trace[16] == 4)
    REQUIRE(!sysctxsnapshot(&runtime_contexts, connection_work.state.self, runtime_key, 0, 4), 98);
  if (resource_trace[16] == 8 || resource_trace[16] == 9)
    REQUIRE(!sysctxsnapshot(&runtime_contexts, connection_work.state.self, runtime_key, 0, 0), 29);
  REQUIRE(!__sys_sigmask(SIGNAL_SETMASK, mask, &ignored), 104);
  resource_trace[19]++;
  runtimehandler(signum);
  __sys_sigreturn((void *) address);
}
static void
runtimeinit(void)
{
  Sigact action;
  REQUIRE(!sysctxinit(&runtime_contexts, connection_work.state.self), 103);
  action.kind = SIGNAL_HANDLER;
  action.handler = (u64) runtimehandler;
  action.trampoline = (u64) runtimetrampoline;
  action.mask = action.flags = action.reserved = 0;
  REQUIRE(!__sys_sigaction(SIGNAL_USR1, &action, NULL), 102);
  resource_trace[17] = (u64) (uintptr_t) __sys_ctxreturn_begin;
  resource_trace[18] = (u64) (uintptr_t) __sys_ctxresume;
}
static void
runtimeinvoke(u64 stage)
{
  resource_trace[16] = stage;
  REQUIRE(!sysctxinvoke(&runtime_contexts, connection_work.state.self, runtime_key), 101);
  if (readforkchild) {
    Ipcmsg      reply, ignored;
    const void *output;
    u64         bytes;
    u32         value;
    REQUIRE((stage == 4 || stage == 8 || stage == 9) && runtime_contexts.contexts[0].active == 0 &&
                sysctxforkreply(&runtime_contexts, runtime_contexts.self, runtime_key, &reply) ==
                    1 &&
                !reply.kind && !reply.source && !reply.request && !reply.image &&
                (isize) reply.words[0] == (stage == 9 ? -ERR_DENIED : 0) &&
                reply.words[2] == (stage == 4   ? 4U
                                   : stage == 8 ? 3U
                                                : 0U) &&
                reply.words[3] == (stage == 4 ? 4U : 0U) &&
                !sysctxsnapshotview(&runtime_contexts, runtime_contexts.self, runtime_key, &output,
                                    &bytes) &&
                (stage == 4 ? bytes == 4 && !__sys_copy(&value, output, sizeof(value)) &&
                                  value == UINT32_C(0x74736574)
                            : bytes == 0) &&
                ipc_rqpoll(readforkrpc, 0, &ignored) == -ERR_BAD_HANDLE &&
                !sysctxfinish(&runtime_contexts, runtime_contexts.self, runtime_key),
            37);
    __sys_exit(43);
  }
  if (stage != 5 && stage != 6)
    resource_trace[16] = 0;
}
static void
writeforkclient(u64 fileid, u64 restored, u64 stage)
{
  Opkey    key;
  Fileslot descriptor;
  u64      file = fileid, ignored;
  u32      i;
  REQUIRE(!sysfilerefpin(&file_registry, connection_work.state.self, fileid) &&
              !sysopprepare(&connection_work, &connection_directory, SYSFILE_MAIN, 1, 510, &file,
                            55, FS_WRITE, 0, &key),
          24);
  for (i = 0; i < sizeof(descriptor); i++)
    ((u8 *) &descriptor)[i] = 0;
  descriptor.seq = key.lease.seq;
  descriptor.state = FS_SLOT_SUBMITTED;
  descriptor.object = 55;
  descriptor.span = 1;
  descriptor.payload_bytes = descriptor.args[1] = 4;
  REQUIRE(!__sys_copy((void *) (uintptr_t) (connection_work.state.map.address + FS_DESC_OFFSET +
                                            key.lease.slot * FS_DESC_BYTES),
                      &descriptor, sizeof(descriptor)) &&
              !__sys_copy((void *) (uintptr_t) (connection_work.state.map.address + FS_DATA_OFFSET +
                                                key.lease.slot * FS_SLOT_BYTES),
                          "once", 4) &&
              !sysctxopcall(&runtime_contexts, connection_work.state.self, 0, &connection_work,
                            &connection_directory, &key, 0, &runtime_key) &&
              !__sys_sigmask(SIGNAL_SETMASK, restored, &ignored),
          25);
  runtimeinvoke(0);
  REQUIRE(!__sys_sigmask(SIGNAL_BLOCK, SIGNAL_SUPPORTED, &ignored) &&
              !sysctxhandoff(&runtime_contexts, connection_work.state.self, runtime_key) &&
              !sysctxfinish(&runtime_contexts, connection_work.state.self, runtime_key) &&
              !sysctxopwait(&runtime_contexts, connection_work.state.self, 0, &connection_work,
                            &connection_directory, &key, &runtime_key) &&
              !__sys_sigmask(SIGNAL_SETMASK, restored, &ignored),
          26);
  runtimeinvoke(stage);
  REQUIRE(!__sys_sigmask(SIGNAL_BLOCK, SIGNAL_SUPPORTED, &ignored) &&
              !sysctxhandoff(&runtime_contexts, connection_work.state.self, runtime_key) &&
              (isize) connection_work.state.operations[key.lease.slot].words[0] ==
                  (stage == 9 ? -ERR_DENIED : 0) &&
              connection_work.state.operations[key.lease.slot].words[2] == (stage == 9 ? 0U : 3U) &&
              !sysctxfinish(&runtime_contexts, connection_work.state.self, runtime_key) &&
              !sysopsaved(&connection_work, &connection_directory, &key) &&
              !sysopack(&connection_work, &connection_directory, &key) &&
              !sysopfinish(&connection_work, &connection_directory, &key, &file) &&
              file == fileid && !sysfilerefunpin(&file_registry, connection_work.state.self, file),
          27);
}
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
  REQUIRE(!ipc_recv(0, 0, &received) && received.kind == IPC_CALL && received.words[0] == 201 &&
              !ipc_reply_resource(received.request, &reply),
          109);
  REQUIRE(!ipc_recv(0, 0, &received) && received.kind == IPC_CALL && received.words[0] == 203, 249);
  words.words[0] = (u64) &reply;
  words.words[1] = 57;
  words.words[2] = 59;
  words.words[3] = 61;
  REQUIRE(ipc_reply_recv(received.request, &words, 0, RECV_NONBLOCK, &received) == -ERR_WOULD_BLOCK,
          250);
  REQUIRE(!ipc_recv(0, 0, &received) && received.kind == IPC_CALL &&
              received.words[0] == FS_HELLO && !received.words[1] && received.words[2] &&
              received.words[3] == UINT64_MAX,
          147);
  {
    Handle    pid, parent;
    Handle    client = received.source;
    u64       image, firstseq = received.words[2], clientimage = received.image;
    Filehead *header = (Filehead *) 0x1100000;
    REQUIRE(!__sys_getid(&pid, &image, &parent), 146);
    arg.bytes = FS_CONN_BYTES;
    REQUIRE(!__sys_alloc(bootresource(view, BOOT_SELF_ALLOC), &arg, &memory), 145);
    map.mem = memory;
    map.length = FS_CONN_BYTES;
    REQUIRE(!__sys_map(space, &map), 144);
    header->magic = FS_CONN_MAGIC;
    header->bytes = FS_CONN_BYTES;
    header->connid = ((u64) 1 << 16) | 5;
    header->serverimage = image;
    header->slots = FS_CONN_SLOTS;
    header->slotbytes = FS_SLOT_BYTES;
    header->reserved[0] = header->reserved[1] = 0;
    reply.source = memory;
    reply.rights = MAP_READ | MAP_WRITE;
    reply.offset = 0;
    reply.length = FS_CONN_BYTES;
    reply.business[0] = received.words[2];
    reply.business[1] = header->connid;
    reply.business[2] = FS_CONN_BYTES;
    REQUIRE(!ipc_reply_resource(received.request, &reply), 143);
    resource_trace[9] = 1;
    REQUIRE(!ipc_recv(0, 0, &received) && received.kind == IPC_CALL && received.source == client &&
                received.image == clientimage && received.words[0] == FS_ATTACH &&
                !received.words[1] && received.words[2] > firstseq && received.words[3] == 55,
            138);
    words.words[0] = 0;
    words.words[1] = received.words[2];
    words.words[2] = ((u64) 1 << 16) | 5;
    words.words[3] = FS_CONN_BYTES;
    REQUIRE(!ipc_reply(received.request, &words), 137);
    resource_trace[9] = 2;
    {
      u64 lastseq = words.words[1];
      u32 attempt;
      for (attempt = 0; attempt < 2; attempt++) {
        REQUIRE(!ipc_recv(0, 0, &received) && received.kind == IPC_CALL &&
                    received.source == client && received.image == clientimage &&
                    received.words[0] == FS_ABORT && !received.words[1] &&
                    received.words[2] > lastseq && received.words[3] == 55,
                137);
        lastseq = received.words[2];
        words.words[0] = words.words[2] = words.words[3] = 0;
        words.words[1] = lastseq;
        REQUIRE(!ipc_reply(received.request, &words), 137);
        resource_trace[53]++;
      }
    }
    {
      u32 attempt;
      for (attempt = 0; attempt < 2; attempt++) {
        REQUIRE(!ipc_recv(0, 0, &received) && received.kind == IPC_CALL &&
                    received.source == client && received.image == clientimage &&
                    received.words[0] == FS_WRITE && received.words[1] == header->connid &&
                    received.words[2] && received.words[3] < FS_CONN_SLOTS,
                23);
        {
          Fileslot           descriptor;
          u32                index = (u32) received.words[3];
          const volatile u8 *data =
              (const volatile u8 *) (uintptr_t) (map.address + FS_DATA_OFFSET +
                                                 index * FS_SLOT_BYTES);
          Fileslot *wire =
              (Fileslot *) (uintptr_t) (map.address + FS_DESC_OFFSET + index * FS_DESC_BYTES);
          REQUIRE(!sysfilerequest((const volatile void *) (uintptr_t) map.address, FS_CONN_BYTES,
                                  index, received.words[2], &descriptor) &&
                      descriptor.object == 55 && descriptor.span == 1 &&
                      descriptor.payload_bytes == 4 && !descriptor.args[0] &&
                      descriptor.args[1] == 4 && !descriptor.args[2] && !descriptor.args[3] &&
                      !descriptor.args[4] && !descriptor.args[5] && !descriptor.args[6] &&
                      !descriptor.args[7] && data[0] == 'o' && data[1] == 'n' && data[2] == 'c' &&
                      data[3] == 'e',
                  22);
          words.words[0] = attempt ? (u64) -ERR_DENIED : 0;
          words.words[3] = 0;
          words.words[1] = received.words[2];
          words.words[2] = attempt ? 0 : 3;
          wire->state = FS_SLOT_DONE;
          REQUIRE(!ipc_reply(received.request, &words), 21);
        }
      }
    }
    REQUIRE(!ipc_recv(0, 0, &received) && received.kind == IPC_CALL && received.source == client &&
                received.image == clientimage && received.words[0] == FS_READ &&
                received.words[1] == header->connid && received.words[2] &&
                received.words[3] < FS_CONN_SLOTS,
            133);
    {
      Fileslot     descriptor;
      u32          index = (u32) received.words[3];
      volatile u8 *data =
          (volatile u8 *) (uintptr_t) (map.address + FS_DATA_OFFSET + index * FS_SLOT_BYTES);
      Fileslot *wire =
          (Fileslot *) (uintptr_t) (map.address + FS_DESC_OFFSET + index * FS_DESC_BYTES);
      REQUIRE(!sysfilerequest((const volatile void *) (uintptr_t) map.address, FS_CONN_BYTES, index,
                              received.words[2], &descriptor) &&
                  descriptor.object == 55 && descriptor.span == 1 &&
                  descriptor.payload_bytes == 4 && !descriptor.args[0] && descriptor.args[1] == 4 &&
                  !descriptor.args[2] && !descriptor.args[3] && !descriptor.args[4] &&
                  !descriptor.args[5] && !descriptor.args[6] && !descriptor.args[7],
              132);
      data[0] = 't';
      data[1] = 'e';
      data[2] = 's';
      data[3] = 't';
      wire->state = FS_SLOT_DONE;
      words.words[0] = 0;
      words.words[1] = received.words[2];
      words.words[2] = words.words[3] = 4;
      REQUIRE(!ipc_reply(received.request, &words), 131);
      resource_trace[9] = 3;
      {
        Handle   delayed_request, delayed_memory;
        Sigsel   signal;
        Fileslot delayed;
        u64      state;
        u32      delayed_slot;
        REQUIRE(!ipc_recv(0, 0, &received) && received.kind == IPC_CALL &&
                    received.source == client && received.image == clientimage &&
                    received.words[0] == FS_READ && received.words[1] == header->connid &&
                    received.words[2] && received.words[3] < FS_CONN_SLOTS,
                92);
        delayed_request = received.request;
        delayed_slot = (u32) received.words[3];
        REQUIRE(!sysfilerequest((const volatile void *) (uintptr_t) map.address, FS_CONN_BYTES,
                                delayed_slot, received.words[2], &delayed) &&
                    delayed.object == 55 && delayed.span == 1 && delayed.args[1] == 4 &&
                    !ipc_defer(delayed_request),
                91);
        resource_trace[16] = 1;
        words.words[0] = 0xa147;
        words.words[1] = words.words[2] = words.words[3] = 0;
        REQUIRE(!ipc_send(bootresource(view, 257), &words), 90);
        signal.kind = SIGNAL_SELECT_PID;
        signal.target = client;
        REQUIRE(!__sys_sigsend(&signal, SIGNAL_USR1), 89);
        words.words[0] = 0xa148;
        REQUIRE(!ipc_send(bootresource(view, 257), &words), 88);
        REQUIRE(!ipc_recv(0, 0, &received) && received.kind == IPC_SEND &&
                    received.source == client && received.words[0] == 0xa149 &&
                    !ipc_rqcheck(delayed_request, &state) &&
                    state == (RQ_CHECK_LIVE | RQ_CHECK_CANCEL),
                87);
        data = (volatile u8 *) (uintptr_t) (map.address + FS_DATA_OFFSET +
                                            delayed_slot * FS_SLOT_BYTES);
        data[0] = 'l';
        data[1] = 'a';
        data[2] = 't';
        data[3] = 'e';
        arg.bytes = PAGE_SIZE;
        REQUIRE(!__sys_alloc(bootresource(view, BOOT_SELF_ALLOC), &arg, &delayed_memory), 86);
        reply.source = delayed_memory;
        reply.rights = MAP_READ;
        reply.offset = 0;
        reply.length = PAGE_SIZE;
        reply.business[0] = 97;
        reply.business[1] = reply.business[2] = 0;
        REQUIRE(!ipc_reply_resource(delayed_request, &reply) && !__sys_free(delayed_memory), 85);
        resource_trace[17] = 1;
      }
      {
        Handle pressure[8], close_request;
        u64    close_seq, state;
        Sigsel signal;
        u32    j;
        for (j = 0; j < 8; j++) {
          REQUIRE(!ipc_recv(0, 0, &received) && received.kind == IPC_CALL &&
                      received.source == client && received.image == clientimage &&
                      received.words[0] == 0xf00d && received.words[1] == j &&
                      !ipc_defer(received.request),
                  121);
          pressure[j] = received.request;
        }
        REQUIRE(!ipc_recv(0, 0, &received) && received.kind == IPC_CALL &&
                    received.source == client && received.image == clientimage &&
                    received.words[0] == FS_CLOSE && received.words[1] == header->connid &&
                    received.words[3] == 6 &&
                    !sysfilerequest((const volatile void *) (uintptr_t) map.address, FS_CONN_BYTES,
                                    6, received.words[2], &descriptor) &&
                    descriptor.object == 55 && descriptor.span == 1 && !descriptor.payload_bytes &&
                    descriptor.args[0] == 77 && !descriptor.args[1] && !descriptor.args[2] &&
                    !descriptor.args[3] && !descriptor.args[4] && !descriptor.args[5] &&
                    !descriptor.args[6] && !descriptor.args[7],
                120);
        close_request = received.request;
        close_seq = received.words[2];
        REQUIRE(!ipc_defer(close_request), 65);
        resource_trace[18] = 1;
        words.words[0] = 0xa14a;
        words.words[1] = words.words[2] = words.words[3] = 0;
        REQUIRE(!ipc_send(bootresource(view, 257), &words), 64);
        signal.kind = SIGNAL_SELECT_PID;
        signal.target = client;
        REQUIRE(!__sys_sigsend(&signal, SIGNAL_USR1), 63);
        words.words[0] = 0xa14c;
        REQUIRE(!ipc_send(bootresource(view, 257), &words), 62);
        REQUIRE(!ipc_recv(0, 0, &received) && received.kind == IPC_CALL &&
                    received.source == client && received.image == clientimage &&
                    received.words[0] == 0xa14b && !ipc_rqcheck(close_request, &state) &&
                    state == RQ_CHECK_LIVE,
                61);
        resource_trace[19] = (state & RQ_CHECK_CANCEL) != 0;
        words.words[0] = 0xa14b;
        REQUIRE(!ipc_reply(received.request, &words), 60);
        resource_trace[21] = 1;
        words.words[0] = 0;
        words.words[1] = close_seq;
        words.words[2] = words.words[3] = 0;
        REQUIRE(!ipc_reply(close_request, &words), 119);
        resource_trace[20] = 1;
        for (j = 0; j < 8; j++) {
          words.words[0] = 0xf00d;
          words.words[1] = j;
          words.words[2] = words.words[3] = 0;
          REQUIRE(!ipc_reply(pressure[j], &words), 118);
        }
        REQUIRE(!__sys_unmap(space, map.address) && !__sys_free(memory), 117);
        resource_trace[9] = 4;
      }
    }
  }
}
static void
checkreceived(Handle handle, u64 subtype, u64 rights, u64 offset, u64 length)
{
  Queryrec rows[4];
  u64      cursor = 0, count, next, i, found = 0;
  do {
    REQUIRE(!__sys_query(0, QUERY_RESOURCE, cursor, rows, 4, &count, &next), 221);
    for (i = 0; i < count; i++)
      if ((Handle) rows[i].data[0] == handle) {
        REQUIRE(rows[i].data[1] == subtype && rows[i].data[2] == rights &&
                    rows[i].data[3] == offset && rows[i].data[4] == length && rows[i].data[5] == 1,
                222);
        found++;
      }
    cursor = next;
  } while (cursor);
  REQUIRE(found == 1, 223);
}
static void
waitsend(u64 tag)
{
  Ipcmsg message;
  isize  error;
  do {
    error = ipc_recv(0, 0, &message);
  } while (error == -ERR_INTERRUPTED);
  REQUIRE(!error && message.kind == IPC_SEND && message.source == connection_peer.pid &&
              message.image == connection_peer.image && message.words[0] == tag,
          84);
}
static void
lateclient(const Bootview *view, Opkey *operation)
{
  Fileslot  descriptor;
  Opkey     background, stale;
  Fileclose closing;
  Ipcmsg    message;
  Msg       words;
  Handle    rpc, resource = 0;
  u64       mask, ignored, generation, now, deadline, file = 0;
  u64       original_file = connection_work.state.operations[operation->lease.slot].file;
  u32       i;
  isize     error;
  for (i = 0; i < sizeof(descriptor); i++)
    ((u8 *) &descriptor)[i] = 0;
  descriptor.seq = operation->lease.seq;
  descriptor.object = 55;
  descriptor.state = FS_SLOT_SUBMITTED;
  descriptor.span = 1;
  descriptor.payload_bytes = descriptor.args[1] = 4;
  REQUIRE(!__sys_sigmask(SIGNAL_BLOCK, SIGNAL_SUPPORTED, &mask) &&
              !__sys_copy((void *) (uintptr_t) (connection_work.state.map.address + FS_DESC_OFFSET +
                                                operation->lease.slot * FS_DESC_BYTES),
                          &descriptor, sizeof(descriptor)) &&
              !sysctxopcall(&runtime_contexts, connection_work.state.self, 0, &connection_work,
                            &connection_directory, operation, CALL_RECV_MEM, &runtime_key) &&
              !sysctxinterruptible(&runtime_contexts, connection_work.state.self, runtime_key) &&
              !__sys_sigmask(SIGNAL_SETMASK, mask, &ignored),
          83);
  runtimeinvoke(5);
  REQUIRE(!__sys_sigmask(SIGNAL_BLOCK, SIGNAL_SUPPORTED, &mask) &&
              !sysctxhandoff(&runtime_contexts, connection_work.state.self, runtime_key) &&
              !__sys_sigmask(SIGNAL_SETMASK, mask, &ignored),
          82);
  waitsend(0xa147);
  waitsend(0xa148);
  REQUIRE(resource_trace[26] == 1 && !__sys_sigmask(SIGNAL_BLOCK, SIGNAL_SUPPORTED, &mask) &&
              !sysctxfinish(&runtime_contexts, connection_work.state.self, runtime_key),
          81);
  resource_trace[16] = 0;
  rpc = connection_work.state.operations[operation->lease.slot].rpc;
  deadline = connection_work.state.operations[operation->lease.slot].deadline;
  for (i = 0; i < sizeof(stale); i++)
    ((u8 *) &stale)[i] = ((const u8 *) operation)[i];
  now = runtimenow();
  if (now < deadline) {
    REQUIRE(sysopresume(&connection_work, &connection_directory, operation, now, &background) ==
                    -ERR_WOULD_BLOCK &&
                !__sys_alarmset(deadline, &generation) &&
                !__sys_sigmask(SIGNAL_SETMASK, mask, &ignored),
            80);
    REQUIRE(!ipc_recv(EVENT_TIMER, RECV_EVENTS_ONLY, &message) && message.kind == IPC_EVENT &&
                message.words[0] & EVENT_TIMER &&
                !__sys_sigmask(SIGNAL_BLOCK, SIGNAL_SUPPORTED, &mask),
            79);
    resource_trace[31] = 1;
  }
  now = runtimenow();
  REQUIRE(now >= deadline &&
              sysopresume(&connection_work, &connection_directory, operation, now, &background) ==
                  SYSOP_ABANDONED &&
              !operation->lease.seq && !operation->owner &&
              connection_work.state.operations[background.lease.slot].rpc == rpc &&
              connection_work.state.operations[background.lease.slot].file == original_file &&
              sysopcancel(&connection_work, &connection_directory, &stale) == -ERR_STALE &&
              sysconnworkrelease(&connection_work, &connection_directory, background.lease) ==
                  -ERR_BUSY &&
              sysfilerefclose(&file_registry, connection_work.state.self, original_file,
                              &closing) == -ERR_BUSY &&
              sysopdrain(&connection_work, &connection_directory, &background, &file) ==
                  -ERR_WOULD_BLOCK &&
              !file,
          78);
  resource_trace[27] = deadline;
  resource_trace[28] = now;
  resource_trace[29] = (u64) rpc;
  REQUIRE(!__sys_sigmask(SIGNAL_SETMASK, mask, &ignored), 77);
  words.words[0] = 0xa149;
  words.words[1] = words.words[2] = words.words[3] = 0;
  REQUIRE(!ipc_send(bootresource(view, 257), &words), 76);
  for (;;) {
    REQUIRE(!__sys_sigmask(SIGNAL_BLOCK, SIGNAL_SUPPORTED, &mask), 75);
    error = sysoppoll(&connection_work, &connection_directory, &background);
    if (!error) {
      resource = connection_work.state.operations[background.lease.slot].resource;
      REQUIRE(resource && !file, 74);
      checkreceived(resource, 4, MAP_READ, 0, PAGE_SIZE);
      error = sysopdrain(&connection_work, &connection_directory, &background, &file);
    }
    if (!error)
      break;
    REQUIRE(error == -ERR_WOULD_BLOCK && !__sys_sigmask(SIGNAL_SETMASK, mask, &ignored), 73);
    REQUIRE(!ipc_recv(EVENT_RQ_DONE, RECV_EVENTS_ONLY, &message), 72);
  }
  REQUIRE(file == original_file &&
              !sysfilerefunpin(&file_registry, connection_work.state.self, file),
          71);
  error = __sys_free(resource);
  REQUIRE(error == -ERR_STALE || error == -ERR_BAD_HANDLE, 70);
  error = ipc_rqpoll(rpc, 0, &message);
  REQUIRE(error == -ERR_STALE || error == -ERR_BAD_HANDLE, 69);
  resource_trace[25] = 1;
  REQUIRE(!__sys_sigmask(SIGNAL_SETMASK, mask, &ignored), 68);
}
static void
closeclient(u64 file)
{
  Fileclose        claimed = {0, 0};
  const Fileclose *anchor = &claimed;
  Filelease        busy;
  Opkey            operation, background, stale;
  Msg              words;
  Ipcmsg           result;
  Fileref          reference;
  Handle           rpc;
  u64              mask, ignored, now, deadline, generation;
  u32              i;
  isize            error;
  REQUIRE(!__sys_sigmask(SIGNAL_BLOCK, SIGNAL_SUPPORTED, &mask), 59);
  now = runtimenow();
  REQUIRE(now <= UINT64_MAX - SYSOP_CONTROL_NS, 58);
  deadline = now + SYSOP_CONTROL_NS;
  resource_trace[33] = deadline;
  resource_trace[39] = now;
  REQUIRE(!sysconnworkreserve(&connection_work, &connection_directory, SYSFILE_CONTROL, 1, &busy) &&
              sysfileclosepick(&file_registry, &connection_work, &connection_directory, 503,
                               deadline, &claimed, &operation, &words) == -ERR_BUSY &&
              !claimed.file && !claimed.sequence &&
              !sysconnworkrelease(&connection_work, &connection_directory, busy) &&
              !sysfileclosepick(&file_registry, &connection_work, &connection_directory, 503,
                                deadline, &claimed, &operation, &words) &&
              claimed.file == file && claimed.sequence &&
              !sysctxopcall(&runtime_contexts, connection_work.state.self, 0, &connection_work,
                            &connection_directory, &operation, 0, &runtime_key) &&
              sysctxinterruptible(&runtime_contexts, connection_work.state.self, runtime_key) ==
                  -ERR_NOTSUP &&
              !__sys_sigmask(SIGNAL_SETMASK, mask, &ignored),
          57);
  runtimeinvoke(6);
  REQUIRE(!__sys_sigmask(SIGNAL_BLOCK, SIGNAL_SUPPORTED, &mask) &&
              !sysctxhandoff(&runtime_contexts, connection_work.state.self, runtime_key) &&
              !__sys_sigmask(SIGNAL_SETMASK, mask, &ignored),
          56);
  waitsend(0xa14a);
  waitsend(0xa14c);
  REQUIRE(resource_trace[32] == 1 && !__sys_sigmask(SIGNAL_BLOCK, SIGNAL_SUPPORTED, &mask) &&
              !sysctxfinish(&runtime_contexts, connection_work.state.self, runtime_key),
          55);
  resource_trace[16] = 0;
  rpc = connection_work.state.operations[6].rpc;
  for (i = 0; i < sizeof(stale); i++)
    ((u8 *) &stale)[i] = ((const u8 *) &operation)[i];
  now = runtimenow();
  if (now < deadline) {
    REQUIRE(sysfileclosewatch(&file_registry, &connection_work, &connection_directory, anchor,
                              &operation, now, &background) == -ERR_WOULD_BLOCK &&
                !__sys_alarmset(deadline, &generation) &&
                !__sys_sigmask(SIGNAL_SETMASK, mask, &ignored),
            54);
    REQUIRE(!ipc_recv(EVENT_TIMER, RECV_EVENTS_ONLY, &result) && result.kind == IPC_EVENT &&
                result.words[0] & EVENT_TIMER &&
                !__sys_sigmask(SIGNAL_BLOCK, SIGNAL_SUPPORTED, &mask),
            53);
    resource_trace[38] = 1;
  }
  now = runtimenow();
  REQUIRE(
      now >= deadline &&
          sysfileclosewatch(&file_registry, &connection_work, &connection_directory, anchor,
                            &operation, now, &background) == SYSFILE_CLOSE_FAULT &&
          !operation.lease.seq && !operation.owner &&
          connection_work.state.operations[6].rpc == rpc &&
          !(connection_work.state.operations[6].flags & SYSOP_CANCEL_SENT) &&
          sysopcancel(&connection_work, &connection_directory, &stale) == -ERR_STALE &&
          sysconnworkrelease(&connection_work, &connection_directory, background.lease) ==
              -ERR_BUSY &&
          !sysfilerefview(&file_registry, connection_work.state.self, anchor->file, &reference) &&
          reference.state == SYSFILE_REF_CLOSING && reference.close_sequence == anchor->sequence &&
          sysfileclosework(&file_registry, &connection_work, &connection_directory, anchor,
                           &background) == -ERR_WOULD_BLOCK,
      52);
  resource_trace[34] = now;
  resource_trace[35] = (u64) rpc;
  resource_trace[36] = 1;
  REQUIRE(!__sys_sigmask(SIGNAL_SETMASK, mask, &ignored), 51);
  words.words[0] = 0xa14b;
  words.words[1] = words.words[2] = words.words[3] = 0;
  REQUIRE(!ipc_call(connection_work.state.call, &words, CALL_CONTROL, &result) && !result.kind &&
              result.words[0] == 0xa14b,
          50);
  for (;;) {
    REQUIRE(!__sys_sigmask(SIGNAL_BLOCK, SIGNAL_SUPPORTED, &mask), 49);
    error = sysfileclosework(&file_registry, &connection_work, &connection_directory, anchor,
                             &background);
    if (!error)
      break;
    REQUIRE(error == -ERR_WOULD_BLOCK && !__sys_sigmask(SIGNAL_SETMASK, mask, &ignored), 48);
    REQUIRE(!ipc_recv(EVENT_RQ_DONE, RECV_EVENTS_ONLY, &result), 47);
  }
  REQUIRE(sysfilerefview(&file_registry, connection_work.state.self, anchor->file, &reference) ==
                  -ERR_STALE &&
              sysfileclosework(&file_registry, &connection_work, &connection_directory, anchor,
                               &background) == -ERR_STALE,
          46);
  error = ipc_rqpoll(rpc, 0, &result);
  REQUIRE(error == -ERR_STALE || error == -ERR_BAD_HANDLE, 45);
  resource_trace[37] = 1;
  REQUIRE(!__sys_sigmask(SIGNAL_SETMASK, mask, &ignored), 44);
}
static void
argumentcheck(const Bootview *view)
{
  Allocarg alloc;
  Maparg   map;
  Argsnap  snapshot;
  Sysret   result;
  u64      pointers[3], mask, ignored, length, probe;
  char     path[8];
  isize    status;
  argument_space = bootresource(view, BOOT_SELF_SPACE);
  alloc.kind = ALLOC_DATA;
  alloc.bytes = 2 * PAGE_SIZE;
  alloc.align = alloc.minaddr = alloc.maxaddr = alloc.reserved = 0;
  REQUIRE(!__sys_alloc(bootresource(view, BOOT_SELF_ALLOC), &alloc, &argument_memory), 40);
  map.mem = argument_memory;
  map.address = 0x1700000;
  map.offset = 0;
  map.length = PAGE_SIZE;
  map.prot = MAP_READ | MAP_WRITE;
  map.reserved = 0;
  REQUIRE(!__sys_map(argument_space, &map), 39);
  map.address = 0x1800000;
  map.offset = PAGE_SIZE;
  REQUIRE(!__sys_map(argument_space, &map), 38);
  argument_vector = map.address + PAGE_SIZE - sizeof(pointers);
  pointers[0] = (u64) (uintptr_t) argument_word;
  pointers[1] = 0x1700000 + PAGE_SIZE - 4;
  pointers[2] = 0;
  REQUIRE(!__sys_copy((void *) (uintptr_t) pointers[1], "abc", 4) &&
              !__sys_copy((void *) (uintptr_t) argument_vector, pointers, sizeof(pointers)) &&
              !__sys_sigmask(SIGNAL_BLOCK, SIGNAL_SUPPORTED, &mask) &&
              !sysargstring(path, sizeof(path), (const char *) (uintptr_t) pointers[1], &length) &&
              length == 4 && path[0] == 'a' && path[1] == 'b' && path[2] == 'c' && !path[3] &&
              !sysctxprepare(&runtime_contexts, connection_work.state.self, 0, SYS_GETID, NULL,
                             &runtime_key) &&
              !sysctxarguments(&runtime_contexts, connection_work.state.self, runtime_key,
                               (const char *const *) (uintptr_t) argument_vector, argument_envp,
                               &snapshot) &&
              snapshot.argc == 2 && snapshot.envc == 1 &&
              !__sys_sigmask(SIGNAL_SETMASK, mask, &ignored),
          37);
  resource_trace[48] = resource_trace[49] = 1;
  runtimeinvoke(7);
  REQUIRE(!__sys_sigmask(SIGNAL_BLOCK, SIGNAL_SUPPORTED, &mask) &&
              !sysctxargumentview(&runtime_contexts, connection_work.state.self, runtime_key,
                                  &snapshot) &&
              argumenttext(&snapshot, 0, 0, "original", 9) &&
              argumenttext(&snapshot, 0, 1, "abc", 4) &&
              argumenttext(&snapshot, 1, 0, "K=one", 6) && argument_word[0] == 'X' &&
              argument_env[2] == 'X' && !argument_memory &&
              __sys_copy(&probe, (const void *) (uintptr_t) argument_vector, sizeof(probe)) ==
                  -ERR_FAULT &&
              !sysctxresult(&runtime_contexts, connection_work.state.self, runtime_key, &status,
                            &result) &&
              !status &&
              !sysctxrearm(&runtime_contexts, connection_work.state.self, runtime_key, SYS_GETID,
                           NULL) &&
              !__sys_sigmask(SIGNAL_SETMASK, mask, &ignored),
          36);
  resource_trace[50] = resource_trace[52] = 1;
  runtimeinvoke(0);
  REQUIRE(!__sys_sigmask(SIGNAL_BLOCK, SIGNAL_SUPPORTED, &mask) &&
              !sysctxargumentview(&runtime_contexts, connection_work.state.self, runtime_key,
                                  &snapshot) &&
              argumenttext(&snapshot, 0, 1, "abc", 4) &&
              !sysctxresult(&runtime_contexts, connection_work.state.self, runtime_key, &status,
                            &result) &&
              !status &&
              !sysctxfinish(&runtime_contexts, connection_work.state.self, runtime_key) &&
              sysctxargumentview(&runtime_contexts, connection_work.state.self, runtime_key,
                                 &snapshot) == -ERR_STALE &&
              !__sys_sigmask(SIGNAL_SETMASK, mask, &ignored),
          35);
  resource_trace[51] = 1;
}
static void
typedclient(const Bootview *view, Handle peer)
{
  Msg       words;
  Ipcmsg    result;
  Handle    rpc, memory, backend, space = bootresource(view, BOOT_SELF_SPACE), denied;
  Handle    donated, extra, redundant = 0;
  Maparg    map;
  Connclaim claim;
  u64       oldmask;
  u32       i;
  claim.tag = connection_tag;
  claim.seq = 1;
  claim.owner = 303;
  sysbootinit(&bootstrap_record);
  REQUIRE(!sysbootprepare(&bootstrap_record, claim, FS_ATTACH, 55, 0), 150);
  words.words[0] = 200;
  words.words[1] = words.words[2] = words.words[3] = 0;
  REQUIRE(!__sys_sigmask(SIGNAL_BLOCK, SIGNAL_SUPPORTED, &oldmask) &&
              !ipc_call(peer, &words, CALL_ASYNC | CALL_RECV_MEM, &result) && result.request > 0,
          224);
  rpc = result.request;
  REQUIRE(!sysbootsubmitted(&bootstrap_record, claim, &result.request) && !result.request &&
              bootstrap_record.rpc == rpc && !__sys_sigmask(SIGNAL_SETMASK, oldmask, &oldmask),
          149);
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
  REQUIRE(!__sys_sigmask(SIGNAL_BLOCK, SIGNAL_SUPPORTED, &oldmask) &&
              !sysbootcapture(&bootstrap_record, claim, 0, &result) &&
              bootstrap_record.state == SYSBOOT_RESULT && bootstrap_record.resource == memory &&
              !__sys_sigmask(SIGNAL_SETMASK, oldmask, &oldmask),
          148);
  checkreceived(memory, 4, MAP_READ | MAP_WRITE, PAGE_SIZE, PAGE_SIZE);
  REQUIRE(!ipc_rqcancel(rpc) && !ipc_rqpoll(rpc, 0, &result) && result.kind == IPC_RESOURCE_REPLY &&
              (Handle) result.words[0] == memory,
          227);
  REQUIRE(!__sys_sigmask(SIGNAL_BLOCK, SIGNAL_SUPPORTED, &oldmask) &&
              !sysbootack(&bootstrap_record, claim) && !bootstrap_record.rpc &&
              sysbootfinish(&bootstrap_record, claim) == -ERR_BUSY &&
              !sysbootresource(&bootstrap_record, claim, &memory) &&
              memory == (Handle) result.words[0] && !sysbootfinish(&bootstrap_record, claim) &&
              !sysbootcheck(&bootstrap_record) && !__sys_sigmask(SIGNAL_SETMASK, oldmask, &oldmask),
          228);
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
  checkreceived(backend, 5, 1, 0, 0);
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
  donated = backend;
  REQUIRE(!__sys_sigmask(SIGNAL_BLOCK, SIGNAL_SUPPORTED, &oldmask) &&
              !sysconnworkinit(&connection_work, &connection_directory, connection_tag) &&
              !sysconnworktakecall(&connection_work, &connection_directory, &backend, &redundant) &&
              !backend && !redundant && connection_work.state.call == donated &&
              !__sys_sigmask(SIGNAL_SETMASK, oldmask, &oldmask),
          237);
  runtimeinit();
  argumentcheck(view);
  words.words[0] = 201;
  REQUIRE(!ipc_call(peer, &words, CALL_RECV_CALL, &result) && result.kind == IPC_RESOURCE_REPLY,
          108);
  extra = (Handle) result.words[0];
  REQUIRE(extra != donated && !__sys_sigmask(SIGNAL_BLOCK, SIGNAL_SUPPORTED, &oldmask) &&
              !sysconnworktakecall(&connection_work, &connection_directory, &extra, &redundant) &&
              !extra && redundant == (Handle) result.words[0] &&
              connection_work.state.call == donated && !__sys_free(redundant) &&
              !__sys_sigmask(SIGNAL_SETMASK, oldmask, &oldmask),
          107);
  redundant = 0;
  words.words[0] = 203;
  REQUIRE(!ipc_call(peer, &words, CALL_RECV_MEM, &result) && !result.kind && result.words[0] &&
              result.words[1] == 57 && result.words[2] == 59 && result.words[3] == 61,
          251);
  {
    Bootmap     mapped = {0, 0, 0, 0, 0, 0};
    Bootreply   validated;
    Connentry   entry;
    Bootstrap  *active = &connection_work.state.bootstrap;
    Filelease   lease, other;
    Fileslot    descriptor;
    Opkey       operation, background, stale, spare, delayed;
    Handle      pressure[8];
    u64         file = 99, sparefile = 100;
    u64         fileid;
    u64         delayed_file;
    u64         stable_args[4], input_bytes, output_bytes;
    const void *input, *output;
    Fileref     reference;
    Fileclose   closing;
    Fdtag       descriptor_tag, last;
    Fdview      sourcefd, duplicatefd;
    Fileref     duplicatefile;
    Fdview      removed;
    u64         displaced, old_generation;
    u32         saved;
    isize       cancellation;
    Handle      original;
    u64         originalva;
    REQUIRE(!__sys_sigmask(SIGNAL_BLOCK, SIGNAL_SUPPORTED, &oldmask) &&
                !sysconnbegin(&connection_directory, connection_tag, 404, &claim) &&
                !sysbootprepare(active, claim, FS_HELLO, UINT64_MAX, 0) &&
                !sysctxbootcall(&runtime_contexts, connection_work.state.self, 0, active, claim,
                                connection_work.state.call, CALL_RECV_MEM, &runtime_key) &&
                sysctxinput(&runtime_contexts, connection_work.state.self, runtime_key,
                            (const void *) 0x1400000, sizeof(input_source)) == -ERR_FAULT &&
                sysctxinputview(&runtime_contexts, connection_work.state.self, runtime_key, &input,
                                &input_bytes) == -ERR_STATE &&
                !sysctxabort(&runtime_contexts, connection_work.state.self, runtime_key) &&
                active->state == SYSBOOT_PREPARED && !active->rpc &&
                !sysctxbootcall(&runtime_contexts, connection_work.state.self, 0, active, claim,
                                connection_work.state.call, CALL_RECV_MEM, &runtime_key) &&
                !sysctxinput(&runtime_contexts, connection_work.state.self, runtime_key,
                             (const void *) input_source, sizeof(input_source)) &&
                !__sys_sigmask(SIGNAL_SETMASK, oldmask, &oldmask),
            142);
    resource_trace[40] = resource_trace[41] = 1;
    runtimeinvoke(1);
    resource_trace[45] = runtime_key.sequence;
    REQUIRE(!__sys_sigmask(SIGNAL_BLOCK, SIGNAL_SUPPORTED, &oldmask) &&
                !sysctxhandoff(&runtime_contexts, connection_work.state.self, runtime_key) &&
                !sysctxrewait(&runtime_contexts, connection_work.state.self, runtime_key),
            141);
    resource_trace[46] = runtime_contexts.contexts[0].sequence;
    REQUIRE(resource_trace[45] == resource_trace[46] &&
                !__sys_sigmask(SIGNAL_SETMASK, oldmask, &oldmask),
            141);
    runtimeinvoke(2);
    REQUIRE(
        !__sys_sigmask(SIGNAL_BLOCK, SIGNAL_SUPPORTED, &oldmask) &&
            !sysctxhandoff(&runtime_contexts, connection_work.state.self, runtime_key) &&
            !sysctxinputview(&runtime_contexts, connection_work.state.self, runtime_key, &input,
                             &input_bytes) &&
            input_bytes == sizeof(stable_args) &&
            !__sys_copy(stable_args, input, sizeof(stable_args)) && stable_args[1] == 4 &&
            input_source[1] == 99 &&
            !sysctxfinish(&runtime_contexts, connection_work.state.self, runtime_key) &&
            !sysbootreply(active, claim, 0, &validated) && validated.memory &&
            validated.bytes == FS_CONN_BYTES && !validated.offset &&
            !sysbootmap(active, claim, space, 0x1200000, connection_peer.image, &mapped) &&
            mapped.mapped && mapped.verified &&
            !sysconnworkadopt(&connection_work, &connection_directory, claim, space, &mapped) &&
            !mapped.mapped && !active->resource && connection_work.state.memory &&
            !sysconnview(&connection_directory, connection_tag, &entry) &&
            entry.state == SYSCONN_READY && entry.connid == validated.connid &&
            !sysbootack(active, claim) && !sysbootfinish(active, claim) &&
            !sysconnworkreserve(&connection_work, &connection_directory, SYSFILE_MAIN, 1, &lease) &&
            !__sys_sigmask(SIGNAL_SETMASK, oldmask, &oldmask),
        140);
    resource_trace[43] = input_bytes;
    original = connection_work.state.memory;
    originalva = connection_work.state.map.address;
    REQUIRE(((volatile Filehead *) originalva)->connid == validated.connid, 139);
    REQUIRE(
        !__sys_sigmask(SIGNAL_BLOCK, SIGNAL_SUPPORTED, &oldmask) &&
            !sysconnreusebegin(&connection_directory, connection_tag, 405, &claim) &&
            !sysbootprepare(active, claim, FS_ATTACH, 55, 0) &&
            !sysbootrequest(active, claim, &words) &&
            !ipc_call(connection_work.state.call, &words, CALL_ASYNC | CALL_RECV_MEM, &result) &&
            !sysbootsubmitted(active, claim, &result.request) &&
            !__sys_sigmask(SIGNAL_SETMASK, oldmask, &oldmask),
        136);
    REQUIRE(!ipc_rqwait(active->rpc, 0, &result) && result.kind == IPC_NONE, 135);
    REQUIRE(!__sys_sigmask(SIGNAL_BLOCK, SIGNAL_SUPPORTED, &oldmask) &&
                !sysbootcapture(active, claim, 0, &result) &&
                !sysconnworkreuse(&connection_work, &connection_directory, claim) &&
                connection_work.state.memory == original &&
                connection_work.state.map.address == originalva &&
                connection_work.state.pages.pages[lease.slot].seq == lease.seq &&
                !sysbootack(active, claim) && !sysbootfinish(active, claim) &&
                !sysconnworkrelease(&connection_work, &connection_directory, lease),
            134);
    {
      Connpeer  otherpeer = connection_peer;
      Conntag   tag;
      Connclaim detached;
      u64       lastseq;
      u32       attempt;
      for (attempt = 0; attempt < SYSCONN_COUNT - 1; attempt++) {
        otherpeer.image++;
        REQUIRE(!sysconnbind(&connection_directory, otherpeer, &tag) &&
                    !sysconnbegin(&connection_directory, tag, 500 + attempt, &detached) &&
                    !sysconnrollback(&connection_directory, detached),
                134);
      }
      otherpeer.image++;
      REQUIRE(sysconnbind(&connection_directory, otherpeer, &tag) == -ERR_CAPACITY, 134);
      lastseq = connection_directory.sequence;
      for (attempt = 0; attempt < 2; attempt++) {
        REQUIRE(!sysconnabortbegin(&connection_directory, 600 + attempt, &detached) &&
                    detached.seq > lastseq && !detached.tag.generation &&
                    detached.tag.index == SYSCONN_COUNT &&
                    !sysbootprepare(&bootstrap_record, detached, FS_ABORT, 55, 0) &&
                    !sysctxbootcall(&runtime_contexts, connection_work.state.self, 0,
                                    &bootstrap_record, detached, connection_work.state.call, 0,
                                    &runtime_key) &&
                    runtime_contexts.contexts[0].args[5] == (CALL_ASYNC | CALL_CONTROL) &&
                    !sysctxinvoke(&runtime_contexts, connection_work.state.self, runtime_key) &&
                    !sysctxhandoff(&runtime_contexts, connection_work.state.self, runtime_key) &&
                    !sysctxrewait(&runtime_contexts, connection_work.state.self, runtime_key) &&
                    !sysctxinvoke(&runtime_contexts, connection_work.state.self, runtime_key) &&
                    !sysctxhandoff(&runtime_contexts, connection_work.state.self, runtime_key) &&
                    !sysbootabortreply(&bootstrap_record, detached) &&
                    !sysctxfinish(&runtime_contexts, connection_work.state.self, runtime_key) &&
                    !sysbootack(&bootstrap_record, detached) &&
                    !sysbootfinish(&bootstrap_record, detached) &&
                    connection_work.state.memory == original &&
                    connection_work.state.map.address == originalva &&
                    sysconnbind(&connection_directory, otherpeer, &tag) == -ERR_CAPACITY,
                134);
        resource_trace[53 + attempt] = detached.seq;
        lastseq = detached.seq;
      }
      resource_trace[55] = 1;
    }
    for (i = 0; i < sizeof(descriptor); i++)
      ((u8 *) &descriptor)[i] = 0;
    sysfdinit(&file_descriptors);
    REQUIRE(!sysfilerefinit(&file_registry, connection_work.state.self), 125);
    referenceprepare(connection_work.state.self, connection_tag, &file);
    fileid = file;
    REQUIRE(
        !sysfdreserve(&file_descriptors, 0, &descriptor_tag) && descriptor_tag.index == 0 &&
            !sysfdpublish(&file_descriptors, descriptor_tag, file, BOOT_FD_CLOEXEC) &&
            !sysfdreserve(&file_descriptors, BOOT_FD_COUNT - 1, &last) &&
            sysfddup(&file_descriptors, &file_registry, connection_work.state.self, 0,
                     BOOT_FD_COUNT - 1, 0) == -ERR_CAPACITY &&
            !sysfilerefview(&file_registry, connection_work.state.self, fileid, &duplicatefile) &&
            duplicatefile.aliases == 1 && !sysfdcancel(&file_descriptors, last) &&
            sysfddup(&file_descriptors, &file_registry, connection_work.state.self, 1, 1, 0) ==
                -ERR_BAD_HANDLE &&
            sysfddup(&file_descriptors, &file_registry, connection_work.state.self, 0, 1, 0) == 1 &&
            !sysfdobserve(&file_descriptors, 0, &sourcefd) && sourcefd.flags == BOOT_FD_CLOEXEC &&
            !sysfdobserve(&file_descriptors, 1, &duplicatefd) && duplicatefd.file == fileid &&
            !duplicatefd.flags &&
            !sysfilerefview(&file_registry, connection_work.state.self, fileid, &duplicatefile) &&
            duplicatefile.aliases == 2 && !sysfdsetflags(&file_descriptors, sourcefd.tag, 0) &&
            !sysfdsetflags(&file_descriptors, duplicatefd.tag, BOOT_FD_CLOEXEC) &&
            !sysfilerefpin(&file_registry, connection_work.state.self, fileid),
        124);
    displaced = 99;
    REQUIRE(
        sysfddup2(&file_descriptors, &file_registry, connection_work.state.self, 1, 1,
                  &displaced) == 1 &&
            !displaced && !sysfdobserve(&file_descriptors, 1, &duplicatefd) &&
            duplicatefd.flags == BOOT_FD_CLOEXEC &&
            sysfddup2(&file_descriptors, &file_registry, connection_work.state.self, 2, 1,
                      &displaced) == -ERR_BAD_HANDLE &&
            !displaced && !sysfdreserve(&file_descriptors, 2, &last) &&
            sysfddup2(&file_descriptors, &file_registry, connection_work.state.self, 0, 2,
                      &displaced) == -ERR_BUSY &&
            !displaced && !sysfdcancel(&file_descriptors, last) &&
            !sysfilerefview(&file_registry, connection_work.state.self, fileid, &duplicatefile) &&
            duplicatefile.aliases == 2,
        124);
    old_generation = duplicatefd.tag.generation;
    REQUIRE(
        sysfddup2(&file_descriptors, &file_registry, connection_work.state.self, 0, 1,
                  &displaced) == 1 &&
            !displaced && !sysfdobserve(&file_descriptors, 1, &duplicatefd) && !duplicatefd.flags &&
            duplicatefd.file == fileid && duplicatefd.tag.generation != old_generation &&
            !sysfilerefview(&file_registry, connection_work.state.self, fileid, &duplicatefile) &&
            duplicatefile.aliases == 2 &&
            !sysfdsetflags(&file_descriptors, duplicatefd.tag, BOOT_FD_CLOEXEC),
        124);
    writeforkclient(fileid, oldmask, 8);
    writeforkclient(fileid, oldmask, 9);
    REQUIRE(!sysopprepare(&connection_work, &connection_directory, SYSFILE_MAIN, 1, 501, &file, 55,
                          FS_READ, 0, &operation) &&
                !file,
            130);
    descriptor.seq = operation.lease.seq;
    descriptor.state = FS_SLOT_SUBMITTED;
    descriptor.object = 55;
    descriptor.span = 1;
    descriptor.payload_bytes = stable_args[1];
    for (i = 0; i < ARRAY_SIZE(stable_args); i++)
      descriptor.args[i] = stable_args[i];
    words.words[0] = FS_READ;
    words.words[1] = connection_work.state.map.connid;
    words.words[2] = operation.lease.seq;
    words.words[3] = operation.lease.slot;
    REQUIRE(!__sys_copy((void *) (uintptr_t) (originalva + FS_DESC_OFFSET +
                                              operation.lease.slot * FS_DESC_BYTES),
                        &descriptor, sizeof(descriptor)) &&
                !sysctxopcall(&runtime_contexts, connection_work.state.self, 0, &connection_work,
                              &connection_directory, &operation, 0, &runtime_key) &&
                !sysctxinterruptible(&runtime_contexts, connection_work.state.self, runtime_key) &&
                !__sys_sigmask(SIGNAL_SETMASK, oldmask, &oldmask),
            129);
    runtimeinvoke(3);
    REQUIRE(!__sys_sigmask(SIGNAL_BLOCK, SIGNAL_SUPPORTED, &oldmask) &&
                !sysctxhandoff(&runtime_contexts, connection_work.state.self, runtime_key) &&
                !sysctxfinish(&runtime_contexts, connection_work.state.self, runtime_key),
            129);
    cancellation =
        ipc_rqpoll(connection_work.state.operations[operation.lease.slot].rpc, 0, &result);
    if (cancellation == -ERR_CANCELLED) {
      REQUIRE(!sysopcapture(&connection_work, &connection_directory, &operation, cancellation,
                            &result) &&
                  !sysopsaved(&connection_work, &connection_directory, &operation) &&
                  !sysopack(&connection_work, &connection_directory, &operation) &&
                  !sysopfinish(&connection_work, &connection_directory, &operation, &file) &&
                  file == fileid &&
                  !sysopprepare(&connection_work, &connection_directory, SYSFILE_MAIN, 1, 504,
                                &file, 55, FS_READ, 0, &operation),
              97);
      descriptor.seq = operation.lease.seq;
      descriptor.state = FS_SLOT_SUBMITTED;
      REQUIRE(
          !__sys_copy((void *) (uintptr_t) (originalva + FS_DESC_OFFSET +
                                            operation.lease.slot * FS_DESC_BYTES),
                      &descriptor, sizeof(descriptor)) &&
              !sysctxopcall(&runtime_contexts, connection_work.state.self, 0, &connection_work,
                            &connection_directory, &operation, 0, &runtime_key) &&
              !sysctxinterruptible(&runtime_contexts, connection_work.state.self, runtime_key) &&
              !__sys_sigmask(SIGNAL_SETMASK, oldmask, &oldmask),
          96);
      runtimeinvoke(0);
      REQUIRE(!__sys_sigmask(SIGNAL_BLOCK, SIGNAL_SUPPORTED, &oldmask) &&
                  !sysctxhandoff(&runtime_contexts, connection_work.state.self, runtime_key) &&
                  !sysctxfinish(&runtime_contexts, connection_work.state.self, runtime_key),
              95);
      resource_trace[24] = 1;
    } else
      REQUIRE(cancellation == -ERR_WOULD_BLOCK || (!cancellation && !result.kind), 94);
    stale.connection.generation = operation.connection.generation;
    stale.connection.index = operation.connection.index;
    stale.connection.reserved = 0;
    stale.lease.seq = operation.lease.seq;
    stale.lease.slot = operation.lease.slot;
    stale.lease.span = operation.lease.span;
    stale.owner = operation.owner;
    REQUIRE(!sysfilerefpin(&file_registry, connection_work.state.self, fileid), 123);
    sparefile = fileid;
    REQUIRE(!sysopbackground(&connection_work, &connection_directory, &operation, &background) &&
                !operation.lease.seq && !operation.owner &&
                sysopcancel(&connection_work, &connection_directory, &stale) == -ERR_STALE &&
                !sysopprepare(&connection_work, &connection_directory, SYSFILE_MAIN, 1, 502,
                              &sparefile, 56, FS_READ, 0, &spare) &&
                spare.lease.slot != background.lease.slot &&
                !sysopabort(&connection_work, &connection_directory, &spare, &sparefile) &&
                sparefile == fileid &&
                !sysfilerefunpin(&file_registry, connection_work.state.self, sparefile),
            128);
    delayed_file = fileid;
    REQUIRE(!sysfilerefpin(&file_registry, connection_work.state.self, fileid) &&
                !sysopprepare(&connection_work, &connection_directory, SYSFILE_MAIN, 1, 505,
                              &delayed_file, 55, FS_READ, 0, &delayed) &&
                !delayed_file,
            67);
    REQUIRE(!sysfdobserve(&file_descriptors, 0, &removed) && removed.file == fileid &&
                !sysfdclose(&file_descriptors, &file_registry, connection_work.state.self,
                            removed.tag, &displaced) &&
                !displaced &&
                sysfddup(&file_descriptors, &file_registry, connection_work.state.self, 1, 0, 0) ==
                    0,
            122);
    displaced = 99;
    REQUIRE(sysfdclose(&file_descriptors, &file_registry, connection_work.state.self, removed.tag,
                       &displaced) == -ERR_STALE &&
                displaced == 99,
            122);
    for (i = 0; i < 2; i++)
      REQUIRE(!sysfdobserve(&file_descriptors, i, &removed) && removed.file == fileid &&
                  !sysfdclose(&file_descriptors, &file_registry, connection_work.state.self,
                              removed.tag, &displaced) &&
                  displaced == (i ? fileid : 0),
              122);
    REQUIRE(!sysfilerefview(&file_registry, connection_work.state.self, fileid, &reference) &&
                reference.state == SYSFILE_REF_CLOSING && !reference.aliases &&
                reference.pins == 2 &&
                sysfilerefnextclose(&file_registry, connection_work.state.self, connection_tag, 0,
                                    &displaced) == -ERR_WOULD_BLOCK &&
                displaced == fileid &&
                sysfilerefclose(&file_registry, connection_work.state.self, fileid, &closing) ==
                    -ERR_BUSY &&
                sysconnworkclean(&connection_work, &connection_directory) == -ERR_BUSY &&
                sysconnworkreserve(&connection_work, &connection_directory, SYSFILE_MAIN, 2,
                                   &other) == -ERR_BUSY &&
                !__sys_sigmask(SIGNAL_SETMASK, oldmask, &oldmask),
            128);
    REQUIRE(!__sys_sigmask(SIGNAL_BLOCK, SIGNAL_SUPPORTED, &oldmask) &&
                !sysctxopwait(&runtime_contexts, connection_work.state.self, 0, &connection_work,
                              &connection_directory, &background, &runtime_key) &&
                !sysctxinput(&runtime_contexts, connection_work.state.self, runtime_key,
                             stable_args, sizeof(stable_args)) &&
                !__sys_sigmask(SIGNAL_SETMASK, oldmask, &oldmask),
            127);
    runtimeinvoke(4);
    REQUIRE(!__sys_sigmask(SIGNAL_BLOCK, SIGNAL_SUPPORTED, &oldmask) &&
                !sysctxhandoff(&runtime_contexts, connection_work.state.self, runtime_key) &&
                !sysctxsnapshot(&runtime_contexts, connection_work.state.self, runtime_key, 0, 4) &&
                !connection_work.state.operations[background.lease.slot].words[0] &&
                connection_work.state.operations[background.lease.slot].words[1] ==
                    background.lease.seq &&
                connection_work.state.operations[background.lease.slot].words[2] == 4 &&
                connection_work.state.operations[background.lease.slot].words[3] == 4 &&
                !sysctxsnapshotview(&runtime_contexts, connection_work.state.self, runtime_key,
                                    &output, &output_bytes) &&
                output_bytes == sizeof(saved) && !__sys_copy(&saved, output, sizeof(saved)) &&
                saved == UINT32_C(0x74736574) &&
                !sysctxfinish(&runtime_contexts, connection_work.state.self, runtime_key) &&
                sysconnworkrelease(&connection_work, &connection_directory, background.lease) ==
                    -ERR_BUSY &&
                sysopack(&connection_work, &connection_directory, &background) == -ERR_STATE &&
                !sysopsaved(&connection_work, &connection_directory, &background) &&
                !sysopack(&connection_work, &connection_directory, &background) &&
                !sysopfinish(&connection_work, &connection_directory, &background, &file) &&
                file == fileid &&
                !sysfilerefunpin(&file_registry, connection_work.state.self, file) &&
                !__sys_sigmask(SIGNAL_SETMASK, oldmask, &oldmask),
            126);
    lateclient(view, &delayed);
    REQUIRE(!__sys_sigmask(SIGNAL_BLOCK, SIGNAL_SUPPORTED, &oldmask) &&
                !sysfilerefnextclose(&file_registry, connection_work.state.self, connection_tag, 0,
                                     &displaced) &&
                displaced == fileid &&
                !sysfilerefview(&file_registry, connection_work.state.self, fileid, &reference) &&
                reference.state == SYSFILE_REF_CLOSING && !reference.aliases && !reference.pins &&
                !reference.close_sequence &&
                !sysfilerefcheck(&file_registry, connection_work.state.self) &&
                sysfilerefconnection(&file_registry, connection_work.state.self, connection_tag) ==
                    1 &&
                connection_work.state.memory == original && connection_work.state.map.mapped &&
                !__sys_sigmask(SIGNAL_SETMASK, oldmask, &oldmask),
            126);
    REQUIRE(!__sys_sigmask(SIGNAL_BLOCK, SIGNAL_SUPPORTED, &oldmask), 116);
    for (i = 0; i < 8; i++) {
      words.words[0] = 0xf00d;
      words.words[1] = i;
      words.words[2] = words.words[3] = 0;
      REQUIRE(!ipc_call(connection_work.state.call, &words, CALL_ASYNC, &result) && result.request,
              115);
      pressure[i] = result.request;
    }
    REQUIRE(ipc_call(connection_work.state.call, &words, CALL_ASYNC, &result) == -ERR_CAPACITY &&
                !result.request && !__sys_sigmask(SIGNAL_SETMASK, oldmask, &oldmask),
            114);
    closeclient(fileid);
    REQUIRE(!__sys_sigmask(SIGNAL_BLOCK, SIGNAL_SUPPORTED, &oldmask) &&
                sysfilerefview(&file_registry, connection_work.state.self, fileid, &reference) ==
                    -ERR_STALE &&
                !sysfilerefconnection(&file_registry, connection_work.state.self, connection_tag) &&
                !__sys_sigmask(SIGNAL_SETMASK, oldmask, &oldmask),
            112);
    for (i = 0; i < 8; i++) {
      REQUIRE(!ipc_rqwait(pressure[i], 0, &result) && result.words[0] == 0xf00d &&
                  result.words[1] == i && !ipc_rqack(pressure[i], ACK_RESULT | ACK_RELEASE),
              111);
    }
    REQUIRE(!sysconninvalidate(&connection_directory, connection_tag) &&
                !sysconnworkclean(&connection_work, &connection_directory) &&
                !connection_work.state.memory && !connection_work.state.map.mapped &&
                !connection_work.state.call,
            110);
    {
      Connpeer old = connection_peer;
      REQUIRE(sysconnpeer(donated, &old) == -ERR_STALE && old.pid == connection_peer.pid &&
                  old.image == connection_peer.image,
              154);
      REQUIRE(ipc_call(donated, &words, 0, &result) == -ERR_BAD_HANDLE, 238);
      REQUIRE(!sysconnpeer(peer, &old) && old.pid == connection_peer.pid &&
                  old.image == connection_peer.image,
              106);
    }
    resource_trace[9] = 6;
  }
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
  u64      image, written, next, state, cursor = 0, found = 0;
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
  do {
    REQUIRE(!__sys_query(0, QUERY_RESOURCE, cursor, records, 16, &written, &next), 116);
    for (i = 0; i < written; i++)
      if ((Handle) records[i].data[0] == peer) {
        REQUIRE(records[i].data[1] == 5 && records[i].data[6] != (u64) pid &&
                    records[i].data[7] == 1,
                117);
        connection_peer.pid = (Handle) records[i].data[6];
        connection_peer.image = records[i].data[7];
        found++;
      }
    cursor = next;
  } while (cursor);
  REQUIRE(found == 1, 117);
  {
    Connpeer  self, unchanged, queried;
    Connclaim claim, nested;
    self.pid = pid;
    self.image = image;
    REQUIRE(!sysconnpeer(peer, &queried) && queried.pid == connection_peer.pid &&
                queried.image == connection_peer.image &&
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
  if (client) {
    typedclient(&view, peer);
  } else
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
