/* PID1 fixture: real blk client I/O, service READY and orderly shutdown. */
#include <proto/blk.h>
#include <proto/init.h>
#include <proto/slot.h>
#include <string.h>
#include <sys/boot.h>
#include <sys/channel.h>
#include <unistd.h>
#include "blkserviceuser.h"
#include "private/bootstrap.h"
#include "private/connection.h"
#include "private/syscall.h"
#include "private/start.h"

volatile u64 blkserviceinittrace[16];

#define CHECK(condition, stage)                                                                    \
  do {                                                                                             \
    if (!(condition))                                                                              \
      failed(stage);                                                                               \
  } while (0)

static NORETURN void
failed(u64 stage)
{
  blkserviceinittrace[14] = stage;
  blkserviceinittrace[15] = 191;
  for (;;)
    __sys_yield();
}

static Conncache cache;
static Connwork  work;
static u64       sequence = 1;

static volatile u8 *
payload(u64 address, u32 slot)
{
  return (volatile u8 *) (uintptr_t) (address + PROTO_DATA_OFFSET + (u64) slot * PROTO_SLOT_BYTES);
}

static void
copy(volatile void *out, const volatile void *in, u32 bytes)
{
  volatile u8       *destination = out;
  const volatile u8 *source = in;
  u32                i;
  for (i = 0; i < bytes; i++)
    destination[i] = source[i];
}

static void
connectservice(const Bootview *boot, Handle call)
{
  Connpeer  self, peer;
  Conntag   tag;
  Connclaim claim;
  Bootmap   map = {0};
  Handle    owned = call, redundant = 0, parent, rpc;
  Msg       hello;
  Ipcmsg    result = {0};
  isize     error;
  CHECK(!__sys_getid(&self.pid, &self.image, &parent), 110);
  CHECK(!sysconninit(&cache, self) && !sysconnpeer(owned, &peer) &&
            !sysconnbind(&cache, peer, &tag) && !sysconnworkinit(&work, &cache, tag) &&
            !sysconnworktakecall(&work, &cache, &owned, &redundant) && !owned && !redundant &&
            !sysconnbegin(&cache, tag, 1, &claim) &&
            !sysbootprepare(&work.state.bootstrap, claim, BLK_HELLO, UINT64_MAX, 0) &&
            !sysbootrequest(&work.state.bootstrap, claim, &hello) && !hello.words[1],
        111);
  sysfilepagesinit(&work.state.pages);
  hello.words[0] = BLK_WORD(BLK_HELLO);
  CHECK(!ipc_call(work.state.call, &hello, CALL_ASYNC | CALL_RECV_MEM, &result), 136);
  rpc = result.request;
  CHECK(!sysbootsubmitted(&work.state.bootstrap, claim, &result.request), 112);
  CHECK(!ipc_rqwait(rpc, 0, &result), 130);
  CHECK(!sysbootcapture(&work.state.bootstrap, claim, 0, &result), 131);
  error = sysbootmap(&work.state.bootstrap, claim, bootresource(boot, BOOT_SELF_SPACE),
                     BLKSERVICE_ADDRESS, peer.image, &map);
  blkserviceinittrace[12] = (u64) error;
  blkserviceinittrace[13] = result.words[0];
  CHECK(!error, 132);
  error = sysconnworkadopt(&work, &cache, claim, bootresource(boot, BOOT_SELF_SPACE), &map);
  blkserviceinittrace[13] = (u64) error;
  CHECK(!error, 133);
  error = sysbootack(&work.state.bootstrap, claim);
  blkserviceinittrace[13] = (u64) error;
  CHECK(!error, 134);
  error = sysbootfinish(&work.state.bootstrap, claim);
  blkserviceinittrace[13] = (u64) error;
  CHECK(!error && work.state.bootstrap.state == SYSBOOT_FREE, 135);
}

static void
callblock(u32 opcode, u64 offset, u32 bytes, u64 geometry, const u8 *write, void *read, u64 stage)
{
  volatile Protoslot *descriptor =
      (volatile Protoslot *) (uintptr_t) (BLKSERVICE_ADDRESS + PROTO_DESC_OFFSET);
  Ipcmsg result = {0};
  Msg    request;
  Handle rpc;
  u64    current;
  u64    checkbits;
  isize  status;
  u32    i;
  if (opcode == BLK_WRITE)
    copy(payload(BLKSERVICE_ADDRESS, 0), write, bytes);
  descriptor[0].payload_bytes = opcode == BLK_INFO ? BLK_INFO_BYTES : bytes;
  descriptor[0].span = 1;
  descriptor[0].args[0] = opcode == BLK_FLUSH ? 0 : offset;
  descriptor[0].args[1] = opcode == BLK_FLUSH ? 0 : bytes;
  descriptor[0].args[2] = geometry;
  for (i = 3; i < 8; i++)
    descriptor[0].args[i] = 0;
  current = ++sequence;
  descriptor[0].seq = current;
  __atomic_store_n(&descriptor[0].state, PROTO_SLOT_SUBMITTED, __ATOMIC_RELEASE);
  request = (Msg){{BLK_WORD(opcode), work.state.map.connid, current, 0}};
  status = ipc_call(work.state.call, &request, CALL_ASYNC, &result);
  blkserviceinittrace[13] = (u64) status;
  CHECK(!status, stage);
  rpc = result.request;
  CHECK(rpc, stage);
  status = ipc_rqwait(rpc, 0, &result);
  blkserviceinittrace[12] = (u64) status;
  blkserviceinittrace[11] = result.words[1];
  blkserviceinittrace[10] = __atomic_load_n(&descriptor[0].state, __ATOMIC_ACQUIRE);
  checkbits =
      (u64) (status != 0) | (u64) (result.kind != IPC_NONE) << 1 | (u64) (result.source != 0) << 2 |
      (u64) (result.request != 0) << 3 | (u64) (result.image != 0) << 4 |
      (u64) (result.words[0] != 0) << 5 | (u64) (result.words[1] != current) << 6 |
      (u64) (__atomic_load_n(&descriptor[0].state, __ATOMIC_ACQUIRE) != PROTO_SLOT_DONE) << 7 |
      (u64) (descriptor[0].seq != current) << 8;
  blkserviceinittrace[13] = checkbits;
  CHECK(!checkbits, stage + 1);
  if (opcode == BLK_INFO) {
    CHECK(result.words[2] && result.words[3] == BLK_INFO_BYTES, stage + 2);
  } else {
    CHECK(result.words[2] == bytes && result.words[3] == bytes, stage + 2);
  }
  if (read)
    copy(read, payload(BLKSERVICE_ADDRESS, 0), opcode == BLK_INFO ? BLK_INFO_BYTES : bytes);
  __atomic_store_n(&descriptor[0].state, PROTO_SLOT_FREE, __ATOMIC_RELEASE);
  CHECK(!ipc_rqack(rpc, ACK_RESULT | ACK_RELEASE), stage + 3);
}

int
main(int argc, char **argv)
{
  const Bootview *boot = sysboot();
  Ipcmsg          ready, reply;
  Msg             call;
  Handle          shutdown = bootresource(boot, BLKSERVICE_INIT_CALL);
  Blkinfo         info = {0};
  u8              write[BLKSERVICE_BLOCK];
  u8              read[BLKSERVICE_BLOCK];
  Handle          pid, service_pid, child, parent;
  u64             image, service, reason, code, waitsequence;
  const char     *argument;
  u32             i;
  CHECK(argc == 2, 100);
  CHECK(!__sys_getid(&pid, &image, &parent) && pid == 1 && !parent && image == 1, 102);
  argument = argv[1];
  CHECK(shutdown > 0 && argument, 103);
  blkserviceinittrace[13] = (u64) (u8) argument[0];
  CHECK(boot->argv[1].length <= 0x7f, 190);
  if (boot->argv[1].length != 2)
    failed(0x80 + boot->argv[1].length);
  service = 0;
  for (i = 0; i + 1 < boot->argv[1].length; i++) {
    CHECK(argument[i] >= '0' && argument[i] <= '9', (u8) argument[i] + 1);
    CHECK(service <= (UINT64_MAX - 9) / 10, 104);
    service = service * 10 + (u64) (argument[i] - '0');
  }
  CHECK(i && service > 1, 105);
  service_pid = (Handle) service;
  CHECK(!ipc_recv(0, 0, &ready) && ready.kind == IPC_CALL && ready.source > 1 &&
            ready.source == service_pid && ready.image == ready.words[3] && ready.request > 0 &&
            INIT_WORD_VERSION(ready.words[0]) == INIT_VERSION &&
            INIT_WORD_OPCODE(ready.words[0]) == INIT_READY && ready.words[1] == 1 &&
            ready.words[2] == 7 && ready.words[3],
        106);
  blkserviceinittrace[0] = (u64) ready.source;
  blkserviceinittrace[1] = ready.words[2];
  blkserviceinittrace[2] = ready.words[3];
  call = (Msg){{0, 1, ready.words[2], ready.words[3]}};
  CHECK(!ipc_reply(ready.request, &call), 107);
  blkserviceinittrace[3] = 1;
  connectservice(boot, shutdown);
  callblock(BLK_INFO, 0, 0, 0, NULL, &info, 120);
  CHECK(info.magic == BLK_INFO_MAGIC && info.version == BLK_VERSION && info.geometry_generation &&
            info.capacity_bytes == BLKSERVICE_BYTES && info.logical_bytes == BLKSERVICE_BLOCK &&
            (info.flags & BLK_FLUSHABLE) && !(info.flags & BLK_READONLY) && !info.reserved[0] &&
            !info.reserved[1] && !info.reserved[2],
        124);
  for (i = 0; i < sizeof(write); i++)
    write[i] = (u8) (0xd6 + i * 29);
  callblock(BLK_WRITE, BLKSERVICE_BLOCK, BLKSERVICE_BLOCK, info.geometry_generation, write, NULL,
            130);
  callblock(BLK_FLUSH, 0, 0, info.geometry_generation, NULL, NULL, 140);
  callblock(BLK_READ, BLKSERVICE_BLOCK, BLKSERVICE_BLOCK, info.geometry_generation, NULL, read,
            150);
  CHECK(!memcmp(write, read, sizeof(write)), 154);
  blkserviceinittrace[4] = info.capacity_bytes;
  blkserviceinittrace[5] = info.logical_bytes;
  blkserviceinittrace[6] = 1;
  blkserviceinittrace[7] = 1;
  blkserviceinittrace[8] = 1;
  sequence = 1;
  call = (Msg){{INIT_WORD(INIT_SHUTDOWN), 0, sequence, 0}};
  CHECK(!ipc_call(shutdown, &call, CALL_CONTROL, &reply), 160);
  CHECK(reply.kind == IPC_NONE && !reply.source && !reply.request && !reply.image &&
            !reply.words[0] && reply.words[1] == sequence && !reply.words[2] && !reply.words[3],
        161);
  CHECK(!__sys_wait(service_pid, 0, &child, &reason, &code, &waitsequence) &&
            child == service_pid && reason == WAIT_EXITED && !code,
        162);
  CHECK(!sysbootunmap(work.state.space, &work.state.map) && !__sys_free(work.state.memory) &&
            !__sys_free(work.state.call),
        163);
  blkserviceinittrace[9] = 1;
  blkserviceinittrace[10] = 1;
  blkserviceinittrace[11] = 1;
  for (;;)
    __sys_yield();
}
