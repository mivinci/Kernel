#include <sys/boot.h>
#include <sys/channel.h>
#include "private/syscall.h"
#include "private/start.h"
#include "private/filecreate.h"
#include "private/close.h"
#include "private/fd.h"
#include <abi/resource.h>

#define ADDRESS 0x1600000UL
#define STOP    0xf001
#define GATE    0xf002
#define CONNID  0x10001
static Connwork work;
#define cache (*(Conncache *) (void *) work.reserved)
static Filepool pool;
static Fdtable  fds;
volatile u64    filecreatetrace[16];
#define CHECK(c)                                                                                   \
  do {                                                                                             \
    if (!(c)) {                                                                                    \
      filecreatetrace[15] = __LINE__;                                                              \
      __sys_exit(190);                                                                             \
    }                                                                                              \
  } while (0)

static void
zero(void *data, u64 bytes)
{
  u64 i;
  for (i = 0; i < bytes; i++)
    ((u8 *) data)[i] = 0;
}
static void
setup(const Bootview *boot)
{
  Handle    self, parent, owned, redundant = 0, rpc;
  u64       image;
  Connpeer  peer;
  Conntag   tag;
  Connclaim claim;
  Bootmap   map;
  Msg       message;
  Ipcmsg    result;
  CHECK(!__sys_getid(&self, &image, &parent));
  cache.self.pid = self;
  cache.self.image = image;
  CHECK(!sysconninit(&cache, cache.self));
  owned = bootresource(boot, 256);
  CHECK(!sysconnpeer(owned, &peer) && !sysconnbind(&cache, peer, &tag) &&
        !sysconnworkinit(&work, &cache, tag) &&
        !sysconnworktakecall(&work, &cache, &owned, &redundant) && !owned && !redundant &&
        !sysconnbegin(&cache, tag, 1, &claim) &&
        !sysbootprepare(&work.state.bootstrap, claim, FS_HELLO, UINT64_MAX, 0) &&
        !sysbootrequest(&work.state.bootstrap, claim, &message));
  CHECK(!ipc_call(work.state.call, &message, CALL_ASYNC | CALL_RECV_MEM, &result));
  rpc = result.request;
  CHECK(!sysbootsubmitted(&work.state.bootstrap, claim, &result.request) &&
        !ipc_rqwait(rpc, 0, &result) && !sysbootcapture(&work.state.bootstrap, claim, 0, &result));
  zero(&map, sizeof(map));
  CHECK(!sysbootmap(&work.state.bootstrap, claim, bootresource(boot, BOOT_SELF_SPACE), ADDRESS,
                    peer.image, &map) &&
        !sysconnworkadopt(&work, &cache, claim, bootresource(boot, BOOT_SELF_SPACE), &map) &&
        !sysbootack(&work.state.bootstrap, claim) && !sysbootfinish(&work.state.bootstrap, claim) &&
        !sysfilerefinit(&pool, work.state.self));
  sysfdinit(&fds);
}
static Opkey
begin(u64 opcode, const char *path, u64 source)
{
  Opkey   key;
  Msg     message;
  Ipcmsg  result;
  Fileref ref;
  u64     args[8], bytes = 0, object = 0;
  zero(args, sizeof(args));
  if (opcode == FS_OPEN) {
    while (path[bytes])
      bytes++;
    bytes++;
    args[3] = bytes;
  }
  if (opcode == FS_DUPREF) {
    CHECK(!sysfilerefview(&pool, work.state.self, source, &ref));
    object = ref.session;
    args[0] = ref.reference;
  }
  if (opcode == FS_PIPE)
    args[1] = 8192;
  CHECK(!sysfilecreateprepare(&pool, &work, &cache, 0, 100 + filecreatetrace[1], opcode, object,
                              &source, args, path, bytes, &key, &message) &&
        !source && !ipc_call(work.state.call, &message, CALL_ASYNC, &result) &&
        !sysopsubmitted(&work, &cache, &key, &result.request));
  filecreatetrace[1]++;
  return key;
}
static void
collect(Opkey *key)
{
  Ipcmsg result;
  Handle rpc;
  CHECK(!sysoprpc(&work, &cache, key, &rpc) && !ipc_rqwait(rpc, 0, &result) &&
        !sysopcapture(&work, &cache, key, 0, &result) &&
        !sysfilecreateresult(&pool, &work, &cache, key));
}
static void
complete(Opkey *key, u32 closing, u64 files[2], u64 *source)
{
  CHECK(!sysfilecreatesettle(&pool, &work, &cache, key, closing) &&
        !sysfilecreateack(&pool, &work, &cache, key) &&
        !sysfilecreatefinish(&pool, &work, &cache, key, files, source));
}
static void
closefile(u64 file, u32 live)
{
  Fileclose anchor;
  Opkey     key;
  Msg       message;
  Ipcmsg    result;
  Handle    rpc;
  if (live)
    CHECK(!sysfilerefdrop(&pool, work.state.self, file));
  CHECK(!sysfilerefclose(&pool, work.state.self, file, &anchor) &&
        !sysfilecloseprepare(&pool, &work, &cache, &anchor, 900 + filecreatetrace[2], 0, &key,
                             &message) &&
        !ipc_call(work.state.call, &message, CALL_ASYNC | CALL_CONTROL, &result));
  rpc = result.request;
  CHECK(!sysopsubmitted(&work, &cache, &key, &result.request) && !ipc_rqwait(rpc, 0, &result) &&
        !sysopcapture(&work, &cache, &key, 0, &result) &&
        !sysfilecloseresult(&pool, &work, &cache, &anchor, &key) &&
        !sysopfinished(&work, &cache, &key) && !sysopsaved(&work, &cache, &key) &&
        !sysopack(&work, &cache, &key) &&
        !sysfileclosecomplete(&pool, &work, &cache, &anchor, &key));
  filecreatetrace[2]++;
}
static void
client(const Bootview *boot)
{
  Opkey  key, background;
  Fdtag  fd;
  Fdview detached;
  u64    files[2], source = 0, first, second, ids[2];
  Msg    gate;
  Ipcmsg result;
  Handle rpc;
  setup(boot);
  key = begin(FS_OPEN, "/a", 0);
  collect(&key);
  zero(files, sizeof(files));
  complete(&key, 0, files, &source);
  first = files[0];
  CHECK(first && !files[1] && !source && !sysfdreserve(&fds, 0, &fd) &&
        !sysfdpublish(&fds, fd, first, 0) && !sysfilerefpin(&pool, work.state.self, first));
  key = begin(FS_DUPREF, 0, first);
  collect(&key);
  zero(files, sizeof(files));
  complete(&key, 0, files, &source);
  second = files[0];
  CHECK(source == first && second != first && !sysfilerefunpin(&pool, work.state.self, source) &&
        !sysfdreserve(&fds, 0, &fd) && !sysfdpublish(&fds, fd, second, 0));
  source = 0;
  key = begin(FS_PIPE, 0, 0);
  ids[0] = work.state.creators[key.lease.slot].files[0];
  ids[1] = work.state.creators[key.lease.slot].files[1];
  collect(&key);
  zero(files, sizeof(files));
  complete(&key, 1, files, &source);
  CHECK(!files[0] && !files[1] && !source);
  closefile(ids[0], 0);
  closefile(ids[1], 0);
  key = begin(FS_OPEN, "/late", 0);
  ids[0] = work.state.creators[key.lease.slot].files[0];
  CHECK(!sysopabandon(&work, &cache, &key, &background));
  gate.words[0] = GATE;
  gate.words[1] = gate.words[2] = gate.words[3] = 0;
  CHECK(!ipc_call(work.state.call, &gate, CALL_ASYNC | CALL_CONTROL, &result));
  rpc = result.request;
  CHECK(!ipc_rqwait(rpc, 0, &result) && !result.words[0] &&
        !ipc_rqack(rpc, ACK_RESULT | ACK_RELEASE));
  collect(&background);
  zero(files, sizeof(files));
  CHECK(sysfilecreatesettle(&pool, &work, &cache, &background, 0) == -ERR_STATE);
  complete(&background, 1, files, &source);
  CHECK(!files[0] && !files[1] && !source);
  closefile(ids[0], 0);
  filecreatetrace[3] = 1;
  key = begin(FS_OPEN, "/missing", 0);
  collect(&key);
  CHECK(work.state.creators[key.lease.slot].error == -2);
  zero(files, sizeof(files));
  complete(&key, 0, files, &source);
  CHECK(!files[0] && !files[1] && !source);
  CHECK(!sysfdobserve(&fds, 0, &detached) && detached.file == first &&
        !sysfddetach(&fds, detached.tag, &detached));
  closefile(first, 1);
  CHECK(!sysfdobserve(&fds, 1, &detached) && detached.file == second &&
        !sysfddetach(&fds, detached.tag, &detached));
  closefile(second, 1);
  gate.words[0] = STOP;
  CHECK(!ipc_call(work.state.call, &gate, CALL_ASYNC, &result));
  rpc = result.request;
  CHECK(!ipc_rqwait(rpc, 0, &result) && result.words[0] == 5 && result.words[1] == 5 &&
        !result.words[2] && !ipc_rqack(rpc, ACK_RESULT | ACK_RELEASE) &&
        !sysfilerefconnection(&pool, work.state.self, work.state.tag) &&
        !sysconninvalidate(&cache, work.state.tag) && !sysconnworkclean(&work, &cache));
  filecreatetrace[0] = 2;
}

static void
server(const Bootview *boot)
{
  Connpeer  peer;
  Allocarg  alloc;
  Maparg    map;
  Resreply  transfer;
  Ipcmsg    input;
  Msg       reply;
  Handle    memory, space = bootresource(boot, BOOT_SELF_SPACE), pending = 0;
  Filehead *head = (Filehead *) ADDRESS;
  Fileslot  descriptor;
  u64       sessions[8], references[8], created = 0, closed = 0, nextsession = 100, nextref = 1000;
  u64       slot, seq, opcode, i, found, number;
  volatile u64 *data;
  CHECK(!sysconnpeer(bootresource(boot, 256), &peer) && !ipc_recv(0, 0, &input) &&
        input.kind == IPC_CALL && input.source == peer.pid && input.image == peer.image &&
        input.words[0] == FS_HELLO && !input.words[1] && input.words[2] &&
        input.words[3] == UINT64_MAX);
  zero(&alloc, sizeof(alloc));
  alloc.bytes = FS_CONN_BYTES;
  alloc.kind = ALLOC_SHARED;
  CHECK(!__sys_alloc(bootresource(boot, BOOT_SELF_ALLOC), &alloc, &memory));
  zero(&map, sizeof(map));
  map.mem = memory;
  map.address = ADDRESS;
  map.length = FS_CONN_BYTES;
  map.prot = 3;
  CHECK(!__sys_map(space, &map));
  zero(head, FS_CONN_BYTES);
  head->magic = FS_CONN_MAGIC;
  head->bytes = FS_CONN_BYTES;
  head->connid = CONNID;
  {
    Handle pid, parent;
    u64    image;
    CHECK(!__sys_getid(&pid, &image, &parent));
    head->serverimage = image;
  }
  head->slots = 8;
  head->slotbytes = 4096;
  zero(&transfer, sizeof(transfer));
  transfer.source = memory;
  transfer.rights = 3;
  transfer.length = FS_CONN_BYTES;
  transfer.business[0] = input.words[2];
  transfer.business[1] = CONNID;
  transfer.business[2] = FS_CONN_BYTES;
  CHECK(!ipc_reply_resource(input.request, &transfer));
  zero(sessions, sizeof(sessions));
  zero(references, sizeof(references));
  for (;;) {
    CHECK(!ipc_recv(0, 0, &input) && input.kind == IPC_CALL && input.source == peer.pid &&
          input.image == peer.image);
    opcode = input.words[0];
    zero(&reply, sizeof(reply));
    if (opcode == STOP) {
      CHECK(!pending && created == 5 && closed == 5);
      for (i = 0; i < created; i++)
        CHECK(!references[i]);
      reply.words[0] = created;
      reply.words[1] = closed;
      CHECK(!ipc_reply(input.request, &reply));
      break;
    }
    if (opcode == GATE) {
      CHECK(pending && !ipc_reply(input.request, &reply));
      reply.words[1] = filecreatetrace[7];
      reply.words[2] = filecreatetrace[8];
      reply.words[3] = 8;
      CHECK(!ipc_reply(pending, &reply));
      pending = 0;
      continue;
    }
    CHECK(input.words[1] == CONNID);
    seq = input.words[2];
    slot = input.words[3];
    CHECK(!sysfilerequest(head, FS_CONN_BYTES, (u32) slot, seq, &descriptor) &&
          descriptor.span == 1);
    data = (volatile u64 *) (ADDRESS + FS_DATA_OFFSET + slot * FS_SLOT_BYTES);
    reply.words[1] = seq;
    if (opcode == FS_CLOSE) {
      CHECK(slot == 6 && !descriptor.payload_bytes && descriptor.args[0]);
      found = 0;
      for (i = 0; i < created; i++)
        if (sessions[i] == descriptor.object && references[i] == descriptor.args[0]) {
          references[i] = 0;
          found++;
        }
      CHECK(found == 1);
      closed++;
    } else if (opcode == FS_OPEN || opcode == FS_DUPREF || opcode == FS_PIPE) {
      if (opcode == FS_OPEN) {
        const volatile char *path = (const volatile char *) data;
        CHECK(!descriptor.object && !descriptor.args[2] &&
              descriptor.args[3] == descriptor.payload_bytes && path[0] == '/');
        if (path[1] == 'm') {
          reply.words[0] = (u64) -2;
          CHECK(!ipc_reply(input.request, &reply));
          continue;
        }
      }
      number = opcode == FS_PIPE ? 2 : 1;
      CHECK(created + number <= 8);
      for (i = 0; i < number; i++) {
        if (opcode == FS_DUPREF) {
          found = 0;
          for (slot = 0; slot < created; slot++)
            if (sessions[slot] == descriptor.object && references[slot] == descriptor.args[0])
              found++;
          CHECK(found == 1);
          sessions[created] = descriptor.object;
        } else
          sessions[created] = nextsession++;
        references[created] = nextref++;
        if (opcode == FS_PIPE) {
          data[i * 2] = sessions[created];
          data[i * 2 + 1] = references[created];
        } else if (opcode == FS_OPEN)
          data[0] = references[created];
        created++;
      }
      reply.words[2] = opcode == FS_PIPE     ? 2
                       : opcode == FS_DUPREF ? references[created - 1]
                                             : sessions[created - 1];
      reply.words[3] = opcode == FS_PIPE ? 32 : opcode == FS_OPEN ? 8 : 0;
      if (opcode == FS_OPEN && descriptor.payload_bytes == 6) {
        CHECK(!pending && !ipc_defer(input.request));
        pending = input.request;
        filecreatetrace[7] = seq;
        filecreatetrace[8] = reply.words[2];
        continue;
      }
    } else
      CHECK(0);
    CHECK(!ipc_reply(input.request, &reply));
  }
  CHECK(!__sys_unmap(space, ADDRESS) && !__sys_free(memory) &&
        !__sys_free(bootresource(boot, 256)));
  filecreatetrace[1] = created;
  filecreatetrace[2] = closed;
  filecreatetrace[0] = 2;
}
int
main(int argc, char **argv)
{
  CHECK(argc == 1 && (argv[0][0] == '0' || argv[0][0] == '1') && !argv[0][1]);
  filecreatetrace[0] = 1;
  if (argv[0][0] == '0')
    client(sysboot());
  else
    server(sysboot());
  return 42;
}
