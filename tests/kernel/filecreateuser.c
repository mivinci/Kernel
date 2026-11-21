#include <errno.h>
#include <signal.h>
#include <sys/boot.h>
#include <sys/channel.h>
#include "private/syscall.h"
#include "private/start.h"
#include "private/filecreate.h"
#include "private/filewatch.h"
#include "private/close.h"
#include "private/fd.h"
#include "private/contextipc.h"
#include "private/sigentry.h"
#include <abi/resource.h>
#include <abi/observe.h>

#define ADDRESS  0x1600000UL
#define STOP     0xf001
#define GATE     0xf002
#define PREPARE  0xf003
#define ENDPEER  0xf004
#define SEEN     0xf005
#define CONNID   0x10001
#define IO_VALUE UINT64_C(0x1122334455667788)
#define IO_WRITE UINT64_C(0x8877665544332211)
#define IO_GHOST UINT64_C(0xdeadbeef5a5a5a5a)
#define IO_GUARD UINT64_C(0x2468ace013579bdf)
static Connwork work, backend_work;
#define cache (*(Conncache *) (void *) work.reserved)
static Filepool pool;
static Fdtable  fds;
static Rbank    contexts;
static Rkey     active_key;
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
        !source &&
        !sysctxcreatecall(&contexts, work.state.self, 0, &pool, &work, &cache, &key, &active_key));
  filecreatetrace[6] = !filecreatetrace[1] ? 1 : 0;
  CHECK(!sysctxinvoke(&contexts, work.state.self, active_key) &&
        !sysctxhandoff(&contexts, work.state.self, active_key) &&
        !sysctxfinish(&contexts, work.state.self, active_key));
  filecreatetrace[6] = 0;
  filecreatetrace[1]++;
  return key;
}
static void
collect(Opkey *key)
{
  CHECK(!sysctxcreatewait(&contexts, work.state.self, 0, &pool, &work, &cache, key, &active_key));
  filecreatetrace[6] = filecreatetrace[10] == 1 ? 2 : 0;
  CHECK(!sysctxinvoke(&contexts, work.state.self, active_key) &&
        !sysctxhandoff(&contexts, work.state.self, active_key) &&
        !sysctxcreateresult(&contexts, work.state.self, active_key) &&
        !sysctxfinish(&contexts, work.state.self, active_key));
  filecreatetrace[6] = 0;
}
static void
complete(Opkey *key, u32 closing, u64 files[2], u64 *source)
{
  CHECK(!sysfilecreatesettle(&pool, &work, &cache, key, closing) &&
        !sysfilecreateack(&pool, &work, &cache, key) &&
        !sysfilecreatefinish(&pool, &work, &cache, key, files, source));
}
static void
closeat(Connwork *connection, u64 file, u32 live)
{
  Fileclose anchor;
  Opkey     key;
  Msg       message;
  Ipcmsg    result;
  Handle    rpc;
  if (live)
    CHECK(!sysfilerefdrop(&pool, work.state.self, file));
  CHECK(!sysfilerefclose(&pool, work.state.self, file, &anchor) &&
        !sysfilecloseprepare(&pool, connection, &cache, &anchor, 900 + filecreatetrace[2], 0, &key,
                             &message) &&
        !ipc_call(connection->state.call, &message, CALL_ASYNC | CALL_CONTROL, &result));
  rpc = result.request;
  CHECK(!sysopsubmitted(connection, &cache, &key, &result.request) &&
        !ipc_rqwait(rpc, 0, &result) && !sysopcapture(connection, &cache, &key, 0, &result) &&
        !sysfilecloseresult(&pool, connection, &cache, &anchor, &key) &&
        !sysopfinished(connection, &cache, &key) && !sysopsaved(connection, &cache, &key) &&
        !sysopack(connection, &cache, &key) &&
        !sysfileclosecomplete(&pool, connection, &cache, &anchor, &key));
  filecreatetrace[2]++;
}
static void
closefile(u64 file, u32 live)
{
  closeat(&work, file, live);
}
static void
bootstrap(Bootstrap *record, Connclaim claim, Handle call, u64 options)
{
  Msg    message;
  Ipcmsg result;
  Handle rpc;
  CHECK(!sysbootrequest(record, claim, &message) &&
        !ipc_call(call, &message, CALL_ASYNC | options, &result));
  rpc = result.request;
  CHECK(!sysbootsubmitted(record, claim, &result.request) && !ipc_rqwait(rpc, 0, &result) &&
        !sysbootcapture(record, claim, 0, &result) && !sysbootack(record, claim));
}
static u64
attachfirst(const Bootview *boot)
{
  Opkey     key;
  Conntag   tag;
  Connclaim claim;
  Connpeer  peer;
  Bootmap   mapped;
  Handle    call;
  u64       files[2], source = 0;
  key = begin(FS_OPEN, "/bound", 0);
  collect(&key);
  CHECK(!sysfilecreateack(&pool, &work, &cache, &key));
  peer = work.state.creators[key.lease.slot].peer;
  CHECK(!sysconnbind(&cache, peer, &tag) && !sysconnworkinit(&backend_work, &cache, tag) &&
        !sysfilecreateattachprepare(&pool, &work, &cache, &key, &backend_work, 0, &claim, &call));
  bootstrap(&backend_work.state.bootstrap, claim, call, CALL_RECV_MEM);
  zero(&mapped, sizeof(mapped));
  CHECK(!sysbootmap(&backend_work.state.bootstrap, claim, bootresource(boot, BOOT_SELF_SPACE),
                    ADDRESS + 0x10000, peer.image, &mapped) &&
        !sysfilecreateattached(&pool, &work, &cache, &key, &backend_work, claim,
                               bootresource(boot, BOOT_SELF_SPACE), &mapped, 0));
  zero(files, sizeof(files));
  CHECK(!sysfilecreatefinish(&pool, &work, &cache, &key, files, &source) && files[0] && !source);
  return files[0];
}
static void
resourceopens(const Bootview *boot)
{
  Opkey     key;
  Conntag   tag;
  Connclaim claim;
  Connpeer  peer;
  Bootmap   mapped;
  Handle    call;
  u64       files[2], source = 0, bound, reused, token;
  u32       i;
  bound = attachfirst(boot);
  CHECK(!sysconnpeer(backend_work.state.call, &peer));
  key = begin(FS_OPEN, "/reuse", 0);
  collect(&key);
  CHECK(!sysfilecreateack(&pool, &work, &cache, &key) &&
        !sysfilecreateattachprepare(&pool, &work, &cache, &key, &backend_work, 0, &claim, &call));
  bootstrap(&backend_work.state.bootstrap, claim, call, CALL_RECV_MEM);
  zero(&mapped, sizeof(mapped));
  CHECK(!sysfilecreateattached(&pool, &work, &cache, &key, &backend_work, claim,
                               bootresource(boot, BOOT_SELF_SPACE), &mapped, 0));
  zero(files, sizeof(files));
  CHECK(!sysfilecreatefinish(&pool, &work, &cache, &key, files, &source) && files[0] && !source);
  reused = files[0];
  for (i = 0; i < SYSCONN_COUNT - 2; i++) {
    peer.image++;
    CHECK(!sysconnbind(&cache, peer, &tag));
  }
  peer.image++;
  CHECK(sysconnbind(&cache, peer, &tag) == -ERR_CAPACITY);
  for (i = 0; i < 2; i++) {
    key = begin(FS_OPEN, i ? "/failed" : "/cancel", 0);
    collect(&key);
    CHECK(!sysfilecreateack(&pool, &work, &cache, &key));
    token = work.state.creators[key.lease.slot].token;
    if (i) {
      CHECK(
          !sysfilecreateattachprepare(&pool, &work, &cache, &key, &backend_work, 0, &claim, &call));
      bootstrap(&backend_work.state.bootstrap, claim, call, CALL_RECV_MEM);
      zero(&mapped, sizeof(mapped));
      mapped.address = ADDRESS;
      CHECK(sysfilecreateattached(&pool, &work, &cache, &key, &backend_work, claim,
                                  bootresource(boot, BOOT_SELF_SPACE), &mapped,
                                  0) == -ERR_INVALID &&
            !sysconnworkreuse(&backend_work, &cache, claim) &&
            !sysbootfinish(&backend_work.state.bootstrap, claim));
    }
    CHECK(!sysfilecreaterevokeprepare(&pool, &work, &cache, &key, 0, 0, &claim, &call));
    bootstrap(&work.state.bootstrap, claim, call, CALL_CONTROL);
    CHECK(!sysfilecreaterevoked(&pool, &work, &cache, &key, claim));
    zero(files, sizeof(files));
    CHECK(!sysfilecreatefinish(&pool, &work, &cache, &key, files, &source) && !files[0] && !source);
    filecreatetrace[4]++;
    if (i) {
      CHECK(!sysconnabortbegin(&cache, 999, &claim) &&
            !sysbootprepare(&work.state.bootstrap, claim, FS_ABORT, token, 0));
      bootstrap(&work.state.bootstrap, claim, backend_work.state.call, CALL_CONTROL);
      CHECK(!sysbootabortreply(&work.state.bootstrap, claim) &&
            !sysbootfinish(&work.state.bootstrap, claim));
      filecreatetrace[4]++;
    }
  }
  CHECK(backend_work.state.memory && backend_work.state.map.verified);
  closeat(&backend_work, bound, 1);
  closeat(&backend_work, reused, 1);
  {
    Msg    message;
    Ipcmsg result;
    message.words[0] = STOP;
    message.words[1] = message.words[2] = message.words[3] = 0;
    CHECK(!ipc_call(backend_work.state.call, &message, 0, &result) && result.words[0] == 4 &&
          result.words[1] == 4 && !sysconninvalidate(&cache, backend_work.state.tag) &&
          !sysconnworkclean(&backend_work, &cache));
  }
  filecreatetrace[5] = 1;
}
static void
creatorhandler(int signum)
{
  Rctx        *outer = &contexts.contexts[0];
  const Opkey *mainkey = &outer->route.operation;
  Createop    *maincreate = &work.state.creators[mainkey->lease.slot];
  Opkey        key;
  Rkey         invocation;
  Msg          message;
  u64          args[8], source = 0, files[2], id, reference;
  CHECK(signum == SIGNAL_USR1 && outer->active && outer->phase == RCTX_CLEANING &&
        outer->route.delivered && outer->route.pool == &pool && mainkey->lease.slot == 0 &&
        sysctxfinish(&contexts, work.state.self, active_key) == -ERR_BUSY);
  reference = maincreate->values[0].reference;
  if (filecreatetrace[6] == 1) {
    CHECK(maincreate->phase == SYSFILE_CREATE_PREPARED &&
          work.state.operations[0].phase == SYSOP_WAITING);
    filecreatetrace[12]++;
  } else {
    CHECK(filecreatetrace[6] == 2 && !outer->route.capture_status &&
          maincreate->phase == SYSFILE_CREATE_CAPTURED && reference &&
          !sysfilerefstaged(&pool, work.state.self, &maincreate->stage));
    filecreatetrace[11]++;
  }
  zero(args, sizeof(args));
  args[3] = 8;
  CHECK(!sysfilecreateprepare(&pool, &work, &cache, SYSFILE_SIGNAL1, 700 + filecreatetrace[10],
                              FS_OPEN, 0, &source, args, "/signal", 8, &key, &message) &&
        key.lease.slot == 2 &&
        !sysctxcreatecall(&contexts, work.state.self, 1, &pool, &work, &cache, &key, &invocation) &&
        !sysctxinvoke(&contexts, work.state.self, invocation) &&
        !sysctxhandoff(&contexts, work.state.self, invocation) &&
        !sysctxfinish(&contexts, work.state.self, invocation) &&
        !sysctxcreatewait(&contexts, work.state.self, 1, &pool, &work, &cache, &key, &invocation) &&
        !sysctxinvoke(&contexts, work.state.self, invocation) &&
        !sysctxhandoff(&contexts, work.state.self, invocation) &&
        !sysctxcreateresult(&contexts, work.state.self, invocation) &&
        !sysctxfinish(&contexts, work.state.self, invocation));
  filecreatetrace[1]++;
  id = work.state.creators[key.lease.slot].files[0];
  zero(files, sizeof(files));
  complete(&key, 1, files, &source);
  CHECK(!files[0] && !source && maincreate->values[0].reference == reference);
  closefile(id, 0);
  filecreatetrace[10]++;
}
static void
creatorsignals(void)
{
  struct sigaction action = {0};
  CHECK(!sysctxinit(&contexts, work.state.self) && !sysctxsigbind(&contexts));
  action.sa_handler = creatorhandler;
  CHECK(!sigaction(SIGUSR1, &action, NULL));
  filecreatetrace[7] = (u64) (uintptr_t) __sys_ctxreturn_begin;
  filecreatetrace[8] = (u64) (uintptr_t) __sys_ctxresume;
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
  creatorsignals();
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
  /* A distinct request tries to reuse the exact deferred OPEN's slot. */
  gate.words[0] = FS_OPEN;
  gate.words[1] = CONNID;
  gate.words[2] = background.lease.seq;
  gate.words[3] = background.lease.slot;
  CHECK(!ipc_call(work.state.call, &gate, CALL_ASYNC, &result));
  rpc = result.request;
  CHECK(!ipc_rqwait(rpc, RQ_FINISH, &result) && !ipc_rqwait(rpc, 0, &result) &&
        (isize) result.words[0] == -EBUSY && result.words[1] == background.lease.seq &&
        !result.words[2] && !result.words[3] && !ipc_rqack(rpc, ACK_RESULT | ACK_RELEASE));
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
  resourceopens(boot);
  gate.words[0] = STOP;
  CHECK(!ipc_call(work.state.call, &gate, CALL_ASYNC, &result));
  rpc = result.request;
  CHECK(!ipc_rqwait(rpc, 0, &result) && result.words[0] == 7 && result.words[1] == 7 &&
        !result.words[2] && !ipc_rqack(rpc, ACK_RESULT | ACK_RELEASE) &&
        !sysfilerefconnection(&pool, work.state.self, work.state.tag) &&
        !sysconninvalidate(&cache, work.state.tag) && !sysconnworkclean(&work, &cache));
  filecreatetrace[0] = 2;
}

static void
exitclient(const Bootview *boot)
{
  Opkey  key;
  Msg    message;
  Ipcmsg result;
  u64    files[2] = {0, 0}, source = 0;
  setup(boot);
  CHECK(!sysctxinit(&contexts, work.state.self));
  key = begin(FS_OPEN, "/a", 0);
  collect(&key);
  complete(&key, 0, files, &source);
  CHECK(files[0] && !files[1] && !source);
  (void) begin(FS_OPEN, "/late", 0);
  message.words[0] = SEEN;
  message.words[1] = message.words[2] = message.words[3] = 0;
  CHECK(!ipc_call(work.state.call, &message, CALL_CONTROL, &result) && !result.words[0]);
  filecreatetrace[0] = 2;
}
static void
server(const Bootview *boot, u32 recover)
{
  Connpeer  peer;
  Allocarg  alloc;
  Maparg    map;
  Resreply  transfer;
  Ipcmsg    input, deferred;
  Msg       reply;
  Handle    memory, space = bootresource(boot, BOOT_SELF_SPACE), pending = 0;
  Filehead *head = (Filehead *) ADDRESS;
  Fileslot  descriptor;
  Filewatch watch = {0};
  u64       sessions[8], references[8], created = 0, closed = 0, nextsession = 100, nextref = 1000;
  u64       slot, seq, opcode, i, found, number, resourcepath;
  volatile u64 *data;
  isize         status;
  CHECK(!sysconnpeer(bootresource(boot, 256), &peer) && !ipc_recv(0, 0, &input) &&
        input.kind == IPC_CALL && input.source == peer.pid && input.image == peer.image &&
        input.words[0] == FS_HELLO && !input.words[1] && input.words[2] &&
        input.words[3] == UINT64_MAX);
  CHECK(!sysfilewatch(&watch, bootresource(boot, 258), &input, CONNID));
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
    CHECK(!ipc_recv(recover == 12 ? EVENT_LIFE : 0, 0, &input));
    if (input.kind == IPC_EVENT) {
      CHECK(recover == 12 && input.words[0] == EVENT_LIFE && !sysfilewatchread(&watch) &&
            watch.owner.reason == OBS_EXIT && pending && created == 2 && !closed && references[0] &&
            references[1]);
      CHECK(sysfilewatchaccept(&watch, head, FS_CONN_BYTES, &deferred, &descriptor) == -ERR_PEER &&
            sysfilewatchclose(&watch) == -ERR_BUSY);
      CHECK(!ipc_rqfinish(pending) && !sysfilefinish(&watch.active, pending));
      references[0] = references[1] = 0;
      closed = 2;
      break;
    }
    CHECK(input.kind == IPC_CALL && input.source == peer.pid && input.image == peer.image);
    opcode = input.words[0];
    zero(&reply, sizeof(reply));
    if (opcode == STOP) {
      CHECK(!pending && created == (recover ? 0 : 7) && closed == (recover ? 0 : 7));
      for (i = 0; i < FS_CONN_SLOTS; i++)
        CHECK(!watch.active.requests[i]);
      for (i = 0; i < created; i++)
        CHECK(!references[i]);
      reply.words[0] = created;
      reply.words[1] = closed;
      CHECK(!ipc_reply(input.request, &reply));
      break;
    }
    if (opcode == ENDPEER) {
      Msg    message;
      Ipcmsg result;
      Handle rpc;
      CHECK(recover && filecreatetrace[4] == 1);
      message.words[0] = ENDPEER;
      message.words[1] = message.words[2] = message.words[3] = 0;
      CHECK(!ipc_call(bootresource(boot, 257), &message, CALL_ASYNC | CALL_CONTROL, &result));
      rpc = result.request;
      filecreatetrace[5] = (u64) rpc;
      CHECK(ipc_rqwait(rpc, 0, &result) == -ERR_PEER && !ipc_rqwait(rpc, RQ_FINISH, &result) &&
            !ipc_rqack(rpc, ACK_RESULT | ACK_RELEASE) && !ipc_reply(input.request, &reply));
      continue;
    }
    if (opcode == GATE) {
      CHECK(pending && !ipc_reply(input.request, &reply));
      reply.words[1] = filecreatetrace[7];
      reply.words[2] = filecreatetrace[8];
      reply.words[3] = 8;
      CHECK(!ipc_reply(pending, &reply) && !sysfilefinish(&watch.active, pending));
      pending = 0;
      continue;
    }
    if (opcode == SEEN) {
      CHECK(recover == 12 && pending && created == 2 && !ipc_reply(input.request, &reply));
      continue;
    }
    CHECK(input.words[1] == CONNID);
    seq = input.words[2];
    slot = input.words[3];
    reply.words[1] = seq;
    status = sysfilewatchaccept(&watch, head, FS_CONN_BYTES, &input, &descriptor);
    if (status == -ERR_BUSY) {
      CHECK(pending && opcode == FS_OPEN && watch.active.requests[slot] == pending);
      reply.words[0] = (u64) -EBUSY;
      CHECK(!ipc_reply(input.request, &reply));
      continue;
    }
    CHECK(!status && descriptor.span == 1);
    data = (volatile u64 *) (ADDRESS + FS_DATA_OFFSET + slot * FS_SLOT_BYTES);
    if ((opcode == FS_READ || opcode == FS_WRITE) && recover >= 9 && recover <= 11) {
      CHECK(created == 1 && references[0] && sessions[0] == descriptor.object && slot < 2 &&
            descriptor.payload_bytes == 8 && !descriptor.args[0] && descriptor.args[1] == 8 &&
            !descriptor.args[2] && !descriptor.args[3]);
      for (i = 4; i < 8; i++)
        CHECK(!descriptor.args[i]);
      filecreatetrace[8]++;
      if (recover == 11 && filecreatetrace[8] == 1) {
        CHECK(opcode == FS_READ);
        data[0] = IO_VALUE;
        reply.words[2] = reply.words[3] = 8;
        CHECK(!ipc_reply(input.request, &reply) && !sysfilefinish(&watch.active, input.request));
        continue;
      }
      CHECK(opcode == (recover == 10 ? FS_WRITE : FS_READ));
      if (opcode == FS_WRITE)
        CHECK(data[0] == IO_WRITE);
      else
        data[0] = IO_GHOST;
      ((volatile Fileslot *) ((u8 *) head + FS_DESC_OFFSET + slot * FS_DESC_BYTES))->state =
          FS_SLOT_DONE;
      filecreatetrace[1] = created;
      filecreatetrace[2] = closed;
      filecreatetrace[0] = 2;
      return; /* Shared DONE without a reply cannot fix this I/O's result. */
    }
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
      resourcepath = 0;
      if (opcode == FS_OPEN) {
        const volatile char *path = (const volatile char *) data;
        CHECK(!descriptor.object && !descriptor.args[2] &&
              descriptor.args[3] == descriptor.payload_bytes && path[0] == '/');
        if (path[1] == 'm') {
          reply.words[0] = (u64) -2;
          CHECK(!ipc_reply(input.request, &reply) && !sysfilefinish(&watch.active, input.request));
          continue;
        }
        resourcepath = path[1] == 'b' || path[1] == 'c' || path[1] == 'f' || path[1] == 'r';
      }
      if (resourcepath) {
        Msg    prepare;
        Ipcmsg result;
        Handle rpc;
        u64    token = ++filecreatetrace[4];
        prepare.words[0] = PREPARE;
        prepare.words[1] = 200 + token;
        prepare.words[2] = 2000 + token;
        prepare.words[3] = token;
        CHECK(!ipc_call(bootresource(boot, 257), &prepare, CALL_ASYNC | CALL_CONTROL, &result));
        rpc = result.request;
        CHECK(!ipc_rqwait(rpc, 0, &result) && !result.words[0] && result.words[1] == token &&
              !ipc_rqack(rpc, ACK_RESULT | ACK_RELEASE));
        data[0] = prepare.words[2];
        zero(&transfer, sizeof(transfer));
        transfer.source = bootresource(boot, 257);
        transfer.rights = RESOURCE_USE;
        transfer.business[0] = seq;
        transfer.business[1] = prepare.words[1];
        transfer.business[2] = token;
        CHECK(!ipc_reply_resource(input.request, &transfer) &&
              !sysfilefinish(&watch.active, input.request));
        if (recover == 13 && token == 2) {
          filecreatetrace[0] = 2;
          return;
        }
        continue;
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
        deferred = input;
        filecreatetrace[7] = seq;
        filecreatetrace[8] = reply.words[2];
        continue;
      }
    } else
      CHECK(0);
    CHECK(!ipc_reply(input.request, &reply) && !sysfilefinish(&watch.active, input.request));
  }
  CHECK(!__sys_unmap(space, ADDRESS) && !__sys_free(memory) &&
        !__sys_free(bootresource(boot, 256)));
  if (recover == 12) {
    Handle pid;
    u64    state, reason, image, cookie = watch.owner.cookie;
    CHECK(!sysfilewatchclose(&watch) && !watch.owner.cookie &&
          __sys_obsread(cookie, &state, &reason, &pid, &image) == -ERR_STALE);
    filecreatetrace[3] = 1;
  } else
    CHECK(!sysfilewatchclose(&watch));
  filecreatetrace[1] = created;
  filecreatetrace[2] = closed;
  filecreatetrace[0] = 2;
}
static void
vfsdeathclient(const Bootview *boot, u32 timeout)
{
  Opkey     key;
  Msg       query;
  Ipcmsg    result;
  Createop *creator;
  u64       bound;
  setup(boot);
  CHECK(!sysctxinit(&contexts, work.state.self));
  bound = attachfirst(boot);
  key = begin(FS_OPEN, "/reuse", 0);
  collect(&key);
  CHECK(!sysfilecreateack(&pool, &work, &cache, &key));
  creator = &work.state.creators[key.lease.slot];
  CHECK(creator->token && creator->call);
  query.words[0] = STOP;
  query.words[1] = query.words[2] = query.words[3] = 0;
  if (timeout) {
    query.words[0] = SEEN;
    CHECK(!ipc_call(creator->call, &query, CALL_CONTROL, &result) && !result.words[0]);
  } else
    CHECK(ipc_call(work.state.call, &query, CALL_CONTROL, &result) == -ERR_PEER);
  query.words[0] = FS_ATTACH;
  query.words[1] = 0;
  query.words[2] = 2;
  query.words[3] = creator->token;
  CHECK(!ipc_call(creator->call, &query, CALL_RECV_MEM, &result) &&
        (isize) result.words[0] == -ESTALE && result.words[1] == 2 && !result.words[2] &&
        !result.words[3]);
  query.words[0] = FS_ABORT;
  query.words[2]++;
  CHECK(!ipc_call(creator->call, &query, CALL_CONTROL, &result) && !result.words[0] &&
        result.words[1] == 3 && !result.words[2] && !result.words[3]);
  closeat(&backend_work, bound, 1);
  query.words[0] = STOP;
  query.words[1] = query.words[2] = query.words[3] = 0;
  CHECK(!ipc_call(backend_work.state.call, &query, CALL_CONTROL, &result) && result.words[0] == 2 &&
        result.words[1] == 2 && !sysconninvalidate(&cache, backend_work.state.tag) &&
        !sysconnworkclean(&backend_work, &cache));
  if (timeout)
    CHECK(!ipc_call(work.state.call, &query, CALL_CONTROL, &result) && !result.words[0]);
  filecreatetrace[0] = 2;
}
static void
backend(const Bootview *boot, u32 recover)
{
  Connpeer  clientpeer, vfspeer;
  Ipcmsg    input;
  Msg       reply;
  Resreply  transfer;
  Allocarg  alloc;
  Maparg    mapping;
  Fileslot  descriptor;
  Fileowner preparer = {0};
  Filewatch watch = {0};
  Handle    memory = 0, space = bootresource(boot, BOOT_SELF_SPACE), waiter = 0;
  Filehead *head = (Filehead *) ADDRESS;
  u64       sessions[4], references[4], created = 0, closed = 0, token, i;
  u64       wanted, attached, aborted, cancelled = 0;
  u64       deadlines[4] = {0, 0, 0, 0}, armed = 0, next, now, generation;
  u32       claimed = 0;
  wanted = recover == 4 ? 1 : recover == 2 ? 2 : recover == 5 ? 3 : 4;
  attached = recover == 4 ? 1 : recover == 2 ? 2 : 3;
  aborted = recover == 4 || recover == 2 ? 1 : recover == 5 ? 2 : 3;
  CHECK(!sysconnpeer(bootresource(boot, 256), &clientpeer) &&
        !sysconnpeer(bootresource(boot, 257), &vfspeer));
  zero(sessions, sizeof(sessions));
  zero(references, sizeof(references));
  for (;;) {
    if (recover == 10) {
      next = 0;
      for (i = 0; i < created; i++)
        if (deadlines[i] && (!next || deadlines[i] < next))
          next = deadlines[i];
      if (next != armed) {
        CHECK(!(next ? __sys_alarmset(next, &generation) : __sys_alarmcancel(&generation)));
        armed = next;
      }
    }
    CHECK(!ipc_recv(recover == 9 ? EVENT_LIFE : recover == 10 ? EVENT_TIMER : 0, 0, &input));
    if (recover == 10) {
      CHECK(!sysfilenow(&now));
      for (i = 0; i < created; i++)
        if (deadlines[i] && now >= deadlines[i]) {
          CHECK(references[i] && !(claimed & (1U << i)));
          deadlines[i] = 0;
          references[i] = 0;
          closed++;
          cancelled++;
        }
      if (input.kind == IPC_EVENT) {
        CHECK(input.words[0] == EVENT_TIMER && cancelled == 1);
        filecreatetrace[6]++;
        if (waiter) {
          zero(&reply, sizeof(reply));
          CHECK(!ipc_reply(waiter, &reply));
          waiter = 0;
        }
        continue;
      }
    }
    if (recover == 9 && preparer.cookie) {
      CHECK(!sysfileownerread(&preparer));
      if (preparer.reason) {
        CHECK(preparer.reason == OBS_EXIT);
        for (i = 0; i < created; i++)
          if (references[i] && !(claimed & (1U << i))) {
            references[i] = 0;
            closed++;
            cancelled++;
          }
        CHECK(!sysfileownerclose(&preparer));
      }
    }
    if (input.kind == IPC_EVENT) {
      CHECK(recover == 9 && input.words[0] == EVENT_LIFE);
      continue;
    }
    CHECK(input.kind == IPC_CALL);
    zero(&reply, sizeof(reply));
    if (input.words[0] == PREPARE) {
      CHECK(input.source == vfspeer.pid && input.image == vfspeer.image && created < 4 &&
            input.words[3] == created + 1 && input.words[1] && input.words[2]);
      CHECK(!sysfileowner(&preparer, bootresource(boot, 258), &input));
      if (recover == 10)
        CHECK(!sysfileopendeadline(&deadlines[created]));
      sessions[created] = input.words[1];
      references[created] = input.words[2];
      created++;
      reply.words[1] = input.words[3];
      CHECK(!ipc_reply(input.request, &reply));
      continue;
    }
    if (input.words[0] == ENDPEER) {
      CHECK((recover == 1 || recover == 6) && input.source == vfspeer.pid &&
            input.image == vfspeer.image && created == 1 && references[0] && !memory);
      filecreatetrace[1] = 1;
      filecreatetrace[2] = 0;
      filecreatetrace[4] = 1;
      filecreatetrace[0] = 2;
      return;
    }
    CHECK(input.source == clientpeer.pid && input.image == clientpeer.image);
    if (recover == 10 && input.words[0] == SEEN) {
      CHECK(!waiter);
      if (filecreatetrace[6])
        CHECK(!ipc_reply(input.request, &reply));
      else {
        CHECK(!ipc_defer(input.request));
        waiter = input.request;
      }
      continue;
    }
    if (input.words[0] == STOP) {
      if (recover >= 9) {
        CHECK(created == 2 && closed == 2 && cancelled == 1 && memory && claimed == 1 &&
              !references[0] && !references[1] && filecreatetrace[3] == 1 &&
              filecreatetrace[4] == 1);
        CHECK(recover == 9 ? !preparer.cookie : filecreatetrace[6] == 1 && !waiter);
        reply.words[0] = created;
        reply.words[1] = closed;
        CHECK(!ipc_reply(input.request, &reply) && !sysfilewatchclose(&watch));
        filecreatetrace[5] = cancelled;
        break;
      }
      CHECK(created == wanted && closed == created && filecreatetrace[3] == attached &&
            filecreatetrace[4] == aborted);
      for (i = 0; i < created; i++)
        CHECK(!references[i]);
      reply.words[0] = created;
      reply.words[1] = closed;
      CHECK(!ipc_reply(input.request, &reply));
      break;
    }
    if (input.words[0] == FS_ABORT || input.words[0] == FS_ATTACH) {
      token = input.words[3];
      CHECK(!input.words[1] && input.words[2] && token && token <= created);
      if (recover == 3 && input.words[0] == FS_ABORT) {
        CHECK(created == 1 && references[0] && !memory);
        filecreatetrace[1] = 1;
        filecreatetrace[4] = 1;
        filecreatetrace[0] = 2;
        return;
      }
      reply.words[1] = input.words[2];
      if (input.words[0] == FS_ABORT) {
        if (references[token - 1]) {
          deadlines[token - 1] = 0;
          references[token - 1] = 0;
          closed++;
        }
        filecreatetrace[4]++;
        CHECK(!ipc_reply(input.request, &reply));
        continue;
      }
      if (!references[token - 1]) {
        reply.words[0] = (u64) -ESTALE;
        CHECK(!ipc_reply(input.request, &reply));
        continue;
      }
      filecreatetrace[3]++;
      if (recover == 7 || (recover == 8 && token == 2)) {
        filecreatetrace[1] = created;
        filecreatetrace[4] = 1;
        filecreatetrace[0] = 2;
        return;
      }
      if (recover == 5 && token == 2) {
        reply.words[0] = (u64) -ERR_DENIED;
        CHECK(!ipc_reply(input.request, &reply));
        continue;
      }
      if (!memory) {
        Handle pid, parent;
        u64    image;
        if (recover >= 9)
          CHECK(!sysfilewatch(&watch, bootresource(boot, 258), &input, CONNID));
        CHECK(!__sys_getid(&pid, &image, &parent));
        zero(&alloc, sizeof(alloc));
        alloc.bytes = FS_CONN_BYTES;
        alloc.kind = ALLOC_SHARED;
        CHECK(!__sys_alloc(bootresource(boot, BOOT_SELF_ALLOC), &alloc, &memory));
        zero(&mapping, sizeof(mapping));
        mapping.mem = memory;
        mapping.address = ADDRESS;
        mapping.length = FS_CONN_BYTES;
        mapping.prot = 3;
        CHECK(!__sys_map(space, &mapping));
        zero(head, FS_CONN_BYTES);
        head->magic = recover == 4 ? 0 : FS_CONN_MAGIC;
        head->bytes = FS_CONN_BYTES;
        head->connid = CONNID;
        head->serverimage = image;
        head->slots = FS_CONN_SLOTS;
        head->slotbytes = FS_SLOT_BYTES;
        zero(&transfer, sizeof(transfer));
        transfer.source = memory;
        transfer.rights = 3;
        transfer.length = FS_CONN_BYTES;
        transfer.business[0] = input.words[2];
        transfer.business[1] = CONNID;
        transfer.business[2] = FS_CONN_BYTES;
        CHECK(!ipc_reply_resource(input.request, &transfer));
      } else if (recover == 5) {
        CHECK(token == 3);
        zero(&transfer, sizeof(transfer));
        transfer.source = memory;
        transfer.rights = 3;
        transfer.length = FS_CONN_BYTES;
        transfer.business[0] = input.words[2];
        transfer.business[1] = CONNID;
        transfer.business[2] = FS_CONN_BYTES;
        CHECK(!ipc_reply_resource(input.request, &transfer));
      } else {
        reply.words[2] = CONNID;
        reply.words[3] = FS_CONN_BYTES;
        CHECK(!ipc_reply(input.request, &reply));
      }
      claimed |= 1U << (token - 1);
      deadlines[token - 1] = 0;
      continue;
    }
    CHECK(input.words[0] == FS_CLOSE && input.words[1] == CONNID && input.words[3] == 6);
    if (recover >= 9)
      CHECK(!sysfilewatchaccept(&watch, head, FS_CONN_BYTES, &input, &descriptor));
    else
      CHECK(!sysfilerequest(head, FS_CONN_BYTES, 6, input.words[2], &descriptor));
    CHECK(descriptor.span == 1 && !descriptor.payload_bytes);
    i = 0;
    while (i < created && (sessions[i] != descriptor.object || references[i] != descriptor.args[0]))
      i++;
    CHECK(i < created && references[i]);
    if (recover >= 9)
      CHECK(i == 0 && cancelled == 1);
    references[i] = 0;
    closed++;
    reply.words[1] = input.words[2];
    CHECK(!ipc_reply(input.request, &reply));
    if (recover >= 9)
      CHECK(!sysfilefinish(&watch.active, input.request));
  }
  CHECK(memory && !__sys_unmap(space, ADDRESS) && !__sys_free(memory) &&
        !__sys_free(bootresource(boot, 256)) && !__sys_free(bootresource(boot, 257)));
  if (recover != 9)
    CHECK(!sysfileownerclose(&preparer));
  filecreatetrace[1] = created;
  filecreatetrace[2] = closed;
  filecreatetrace[0] = 2;
}
static void
deadclient(const Bootview *boot)
{
  Opkey     key;
  Fileclose anchor;
  Fileref   reference;
  Msg       message;
  Ipcmsg    result;
  Handle    rpc;
  u64       files[2], source = 0, first, second;
  u32       steps = 0;
  isize     error;
  setup(boot);
  CHECK(!sysctxinit(&contexts, work.state.self));
  key = begin(FS_OPEN, "/a", 0);
  collect(&key);
  zero(files, sizeof(files));
  complete(&key, 0, files, &source);
  first = files[0];
  key = begin(FS_OPEN, "/q", 0);
  collect(&key);
  zero(files, sizeof(files));
  complete(&key, 0, files, &source);
  second = files[0];
  CHECK(first && second && first != second && !sysfilerefdrop(&pool, work.state.self, first) &&
        !sysfilerefclose(&pool, work.state.self, first, &anchor) &&
        !sysfilecloseprepare(&pool, &work, &cache, &anchor, 800, 0, &key, &message) &&
        !ipc_call(work.state.call, &message, CALL_ASYNC | CALL_CONTROL, &result));
  rpc = result.request;
  CHECK(!sysopsubmitted(&work, &cache, &key, &result.request) &&
        ipc_rqwait(rpc, 0, &result) == -ERR_PEER &&
        !sysopcapture(&work, &cache, &key, -ERR_PEER, &result) &&
        sysfilecloseresult(&pool, &work, &cache, &anchor, &key) == -ERR_PEER &&
        !sysoppeerended(&work, &cache, &key) && work.state.ended == 1);
  CHECK(sysfilecloserecover(&pool, &work, &cache, &anchor, &key) == -ERR_WOULD_BLOCK);
  filecreatetrace[5] = (u64) rpc;
  filecreatetrace[6] = 6;
  do {
    CHECK(++steps < 64 && !__sys_yield());
    error = sysfilecloserecover(&pool, &work, &cache, &anchor, &key);
  } while (error == -ERR_WOULD_BLOCK);
  CHECK(!error && sysfilerefview(&pool, work.state.self, first, &reference) == -ERR_STALE &&
        !sysfilerefview(&pool, work.state.self, second, &reference) &&
        reference.state == SYSFILE_REF_LIVE && reference.aliases == 1 &&
        !sysfilerefdrop(&pool, work.state.self, second) &&
        !sysfilerefclose(&pool, work.state.self, second, &anchor) &&
        !sysfilecloseended(&pool, &work, &cache, &anchor) &&
        !sysfilerefconnection(&pool, work.state.self, work.state.tag) &&
        !sysconnworkclean(&work, &cache) && !sysconnforget(&cache, work.state.tag));
  filecreatetrace[2] = 2;
  filecreatetrace[3] = 1;
  filecreatetrace[4] = steps;
  filecreatetrace[0] = 2;
}
static void
createdeadclient(const Bootview *boot, u64 opcode)
{
  Opkey key, retry;
  Msg   message;
  u64   args[8], files[2], source = 0;
  u32   steps = 0, i;
  isize status, error;
  setup(boot);
  CHECK(!sysctxinit(&contexts, work.state.self));
  key = begin(opcode, opcode == FS_OPEN ? "/a" : NULL, 0);
  CHECK(!sysctxcreatewait(&contexts, work.state.self, 0, &pool, &work, &cache, &key, &active_key));
  status = sysctxinvoke(&contexts, work.state.self, active_key);
  CHECK(status == -ERR_PEER && !sysctxhandoff(&contexts, work.state.self, active_key) &&
        !sysctxfinish(&contexts, work.state.self, active_key));
  zero(args, sizeof(args));
  args[3] = 3;
  CHECK(
      !sysfilecreateprepare(&pool, &work, &cache, SYSFILE_MAIN, 900, FS_OPEN, 0, &source, args,
                            "/q", 3, &retry, &message) &&
      !sysctxcreatecall(&contexts, work.state.self, 0, &pool, &work, &cache, &retry, &active_key) &&
      sysctxinvoke(&contexts, work.state.self, active_key) == -ERR_PEER &&
      !sysctxhandoff(&contexts, work.state.self, active_key) &&
      !sysctxpeerended(&contexts, work.state.self, active_key) &&
      !sysctxfinish(&contexts, work.state.self, active_key) &&
      !sysfilecreateabort(&pool, &work, &cache, &retry, &source) && !source &&
      sysfilecreatefailed(&pool, &work, &cache, &key) == -ERR_WOULD_BLOCK);
  filecreatetrace[7] = 1;
  filecreatetrace[8] = (u64) status;
  filecreatetrace[5] = (u64) work.state.operations[key.lease.slot].rpc;
  filecreatetrace[6] = 6;
  do {
    CHECK(++steps < 64 && !__sys_yield());
    error = sysfilecreatefailed(&pool, &work, &cache, &key);
  } while (error == -ERR_WOULD_BLOCK);
  CHECK(!error && work.state.creators[key.lease.slot].error == status);
  zero(files, sizeof(files));
  complete(&key, 0, files, &source);
  CHECK(!files[0] && !files[1] && !source);
  for (i = 0; i < SYSFILE_REFS; i++)
    CHECK(pool.entries[i].state == SYSFILE_REF_FREE ||
          pool.entries[i].state == SYSFILE_REF_RETIRED);
  CHECK(!sysconnworkclean(&work, &cache) && !sysconnforget(&cache, work.state.tag));
  filecreatetrace[2] = opcode == FS_PIPE ? 3 : 2;
  filecreatetrace[3] = 1;
  filecreatetrace[4] = steps;
  filecreatetrace[0] = 2;
}
static void
deadserver(const Bootview *boot, u64 creating)
{
  Connpeer  peer;
  Allocarg  alloc;
  Maparg    mapping;
  Resreply  transfer;
  Ipcmsg    input;
  Msg       reply;
  Filehead *head = (Filehead *) ADDRESS;
  Fileslot  descriptor;
  Handle    memory, pid, parent;
  u64       image, i;
  CHECK(!sysconnpeer(bootresource(boot, 256), &peer) && !ipc_recv(0, 0, &input) &&
        input.kind == IPC_CALL && input.source == peer.pid && input.image == peer.image &&
        input.words[0] == FS_HELLO && !input.words[1]);
  if (creating == FS_HELLO) {
    filecreatetrace[1] = 1;
    filecreatetrace[0] = 2;
    return;
  }
  zero(&alloc, sizeof(alloc));
  alloc.bytes = FS_CONN_BYTES;
  alloc.kind = ALLOC_SHARED;
  CHECK(!__sys_alloc(bootresource(boot, BOOT_SELF_ALLOC), &alloc, &memory));
  zero(&mapping, sizeof(mapping));
  mapping.mem = memory;
  mapping.address = ADDRESS;
  mapping.length = FS_CONN_BYTES;
  mapping.prot = 3;
  CHECK(!__sys_map(bootresource(boot, BOOT_SELF_SPACE), &mapping) &&
        !__sys_getid(&pid, &image, &parent));
  zero(head, FS_CONN_BYTES);
  head->magic = FS_CONN_MAGIC;
  head->bytes = FS_CONN_BYTES;
  head->connid = CONNID;
  head->serverimage = image;
  head->slots = FS_CONN_SLOTS;
  head->slotbytes = FS_SLOT_BYTES;
  zero(&transfer, sizeof(transfer));
  transfer.source = memory;
  transfer.rights = 3;
  transfer.length = FS_CONN_BYTES;
  transfer.business[0] = input.words[2];
  transfer.business[1] = CONNID;
  transfer.business[2] = FS_CONN_BYTES;
  CHECK(!ipc_reply_resource(input.request, &transfer));
  if (creating) {
    CHECK(!ipc_recv(0, 0, &input) && input.kind == IPC_CALL && input.source == peer.pid &&
          input.image == peer.image && input.words[0] == creating);
    if (creating == FS_ATTACH)
      CHECK(!input.words[1] && input.words[2] && input.words[3] == 1);
    else
      CHECK(input.words[1] == CONNID && !sysfilerequest(head, FS_CONN_BYTES, (u32) input.words[3],
                                                        input.words[2], &descriptor));
    filecreatetrace[1] = 1;
    filecreatetrace[0] = 2;
    return;
  }
  for (i = 0; i < 2; i++) {
    CHECK(!ipc_recv(0, 0, &input) && input.kind == IPC_CALL && input.source == peer.pid &&
          input.image == peer.image && input.words[0] == FS_OPEN && input.words[1] == CONNID &&
          !sysfilerequest(head, FS_CONN_BYTES, (u32) input.words[3], input.words[2], &descriptor));
    *(volatile u64 *) (ADDRESS + FS_DATA_OFFSET + input.words[3] * FS_SLOT_BYTES) = 1000 + i;
    zero(&reply, sizeof(reply));
    reply.words[1] = input.words[2];
    reply.words[2] = 100 + i;
    reply.words[3] = 8;
    CHECK(!ipc_reply(input.request, &reply));
  }
  CHECK(!ipc_recv(0, 0, &input) && input.kind == IPC_CALL && input.source == peer.pid &&
        input.image == peer.image && input.words[0] == FS_CLOSE && input.words[1] == CONNID &&
        input.words[3] == 6);
  filecreatetrace[1] = 2;
  filecreatetrace[2] = 1;
  filecreatetrace[0] = 2;
  /* Exit with the accepted CLOSE unanswered and both references still live.
     Kernel Image termination, not a fabricated positive reply, drives recovery. */
}
static void
resourceendclient(const Bootview *boot, u32 submitted)
{
  Opkey     key;
  Connpeer  peer;
  Conntag   tag;
  Connclaim claim;
  Connentry entry;
  Handle    call, rpc;
  Rkey      invocation;
  Msg       message;
  Ipcmsg    result;
  u64       files[2], source = 0;
  u32       i, steps = 0;
  isize     error;
  setup(boot);
  CHECK(!sysctxinit(&contexts, work.state.self));
  key = begin(FS_OPEN, "/bound", 0);
  collect(&key);
  CHECK(!sysfilecreateack(&pool, &work, &cache, &key));
  peer = work.state.creators[key.lease.slot].peer;
  for (i = 0; i < SYSCONN_COUNT - 1; i++) {
    peer.image++;
    CHECK(!sysconnbind(&cache, peer, &tag));
  }
  peer = work.state.creators[key.lease.slot].peer;
  CHECK(sysconnbind(&cache, peer, &tag) == -ERR_CAPACITY);
  message.words[0] = ENDPEER;
  message.words[1] = message.words[2] = message.words[3] = 0;
  if (!submitted) {
    CHECK(!ipc_call(work.state.call, &message, CALL_ASYNC | CALL_CONTROL, &result));
    rpc = result.request;
    CHECK(!ipc_rqwait(rpc, 0, &result) && !result.words[0] &&
          !ipc_rqack(rpc, ACK_RESULT | ACK_RELEASE));
  }
  CHECK(!sysfilecreaterevokeprepare(&pool, &work, &cache, &key, 0, 0, &claim, &call) &&
        !sysctxbootcall(&contexts, work.state.self, 0, &work.state.bootstrap, claim, call, 0,
                        &invocation));
  if (submitted) {
    CHECK(!sysctxinvoke(&contexts, work.state.self, invocation) &&
          !sysctxhandoff(&contexts, work.state.self, invocation) &&
          !sysctxrewait(&contexts, work.state.self, invocation));
  }
  CHECK(
      sysctxinvoke(&contexts, work.state.self, invocation) == -ERR_PEER &&
      !sysctxhandoff(&contexts, work.state.self, invocation) &&
      !sysctxcreatepeerended(&contexts, work.state.self, invocation, &pool, &work, &cache, &key) &&
      !sysctxfinish(&contexts, work.state.self, invocation));
  if (submitted) {
    CHECK(sysfilecreateended(&pool, &work, &cache, &key) == -ERR_WOULD_BLOCK);
    filecreatetrace[5] = (u64) work.state.bootstrap.rpc;
    filecreatetrace[6] = 6;
    do {
      CHECK(++steps < 64 && !__sys_yield());
      error = sysfilecreateended(&pool, &work, &cache, &key);
    } while (error == -ERR_WOULD_BLOCK);
    CHECK(!error);
  } else
    CHECK(!sysfilecreateended(&pool, &work, &cache, &key));
  zero(files, sizeof(files));
  CHECK(!sysfilecreatefinish(&pool, &work, &cache, &key, files, &source) && !files[0] && !source &&
        !work.state.ended && !sysconnview(&cache, work.state.tag, &entry) &&
        entry.state == SYSCONN_READY && sysconnbind(&cache, peer, &tag) == -ERR_CAPACITY);
  message.words[0] = STOP;
  CHECK(!ipc_call(work.state.call, &message, 0, &result) && !result.words[0] &&
        !sysconninvalidate(&cache, work.state.tag) && !sysconnworkclean(&work, &cache));
  filecreatetrace[2] = 1;
  filecreatetrace[3] = 1;
  filecreatetrace[4] = steps;
  filecreatetrace[0] = 2;
}
static void
iodraft(u64 file, u64 opcode, u64 owner, Opkey *key)
{
  Fileref  reference;
  Fileslot descriptor;
  u64      pin = file, value = IO_WRITE;
  Rkey     invocation;
  CHECK(!sysfilerefview(&pool, work.state.self, file, &reference) &&
        !sysfilerefpin(&pool, work.state.self, file) &&
        !sysopprepare(&work, &cache, SYSFILE_MAIN, 1, owner, &pin, reference.session, opcode, 0,
                      key));
  zero(&descriptor, sizeof(descriptor));
  descriptor.seq = key->lease.seq;
  descriptor.state = FS_SLOT_SUBMITTED;
  descriptor.payload_bytes = 8;
  descriptor.object = reference.session;
  descriptor.span = 1;
  descriptor.args[1] = 8;
  CHECK(!__sys_copy((void *) (uintptr_t) (work.state.map.address + FS_DESC_OFFSET +
                                          key->lease.slot * FS_DESC_BYTES),
                    &descriptor, sizeof(descriptor)));
  if (opcode == FS_WRITE)
    CHECK(!__sys_copy((void *) (uintptr_t) (work.state.map.address + FS_DATA_OFFSET +
                                            key->lease.slot * FS_SLOT_BYTES),
                      &value, 8));
  CHECK(!sysctxopcall(&contexts, work.state.self, 0, &work, &cache, key, 0, &invocation) &&
        !sysctxinvoke(&contexts, work.state.self, invocation) &&
        !sysctxhandoff(&contexts, work.state.self, invocation) &&
        !sysctxfinish(&contexts, work.state.self, invocation));
}
static void
iorecoverclient(const Bootview *boot, u32 mode)
{
  Opkey     open, failed, success;
  Fileclose anchor;
  Fileref   held;
  Rkey      invocation;
  u64       files[2], source = 0, file, pin = 0, copied = IO_GUARD, steps = 0;
  isize     outcome, error;
  setup(boot);
  CHECK(!sysctxinit(&contexts, work.state.self));
  open = begin(FS_OPEN, "/a", 0);
  collect(&open);
  CHECK(!sysfilecreateack(&pool, &work, &cache, &open) &&
        !sysfilecreatesettle(&pool, &work, &cache, &open, 0));
  zero(files, sizeof(files));
  CHECK(!sysfilecreatefinish(&pool, &work, &cache, &open, files, &source) && files[0] && !source);
  file = files[0];
  if (mode == 2)
    iodraft(file, FS_READ, 801, &success);
  iodraft(file, mode == 1 ? FS_WRITE : FS_READ, 802, &failed);
  CHECK(!sysctxopwait(&contexts, work.state.self, 0, &work, &cache, &failed, &invocation));
  outcome = sysctxinvoke(&contexts, work.state.self, invocation);
  CHECK(outcome == -ERR_PEER && !sysctxhandoff(&contexts, work.state.self, invocation) &&
        !sysctxfinish(&contexts, work.state.self, invocation) &&
        !sysoppeerended(&work, &cache, &failed) &&
        ((const volatile Fileslot *) (uintptr_t) (work.state.map.address + FS_DESC_OFFSET +
                                                  failed.lease.slot * FS_DESC_BYTES))
                ->state == FS_SLOT_DONE &&
        sysopcopy(&work, &cache, &failed, 0, 8, &copied) == -ERR_STATE && copied == IO_GUARD &&
        !sysfilerefdrop(&pool, work.state.self, file) &&
        sysfilerefclose(&pool, work.state.self, file, &anchor) == -ERR_BUSY &&
        sysoprecover(&work, &cache, &failed, &pin) == -ERR_WOULD_BLOCK && !pin &&
        !sysfilerefview(&pool, work.state.self, file, &held) && held.pins == (mode == 2 ? 2U : 1U));
  filecreatetrace[5] = (u64) work.state.operations[failed.lease.slot].rpc;
  filecreatetrace[6] = 11;
  do {
    CHECK(++steps < 64 && !__sys_yield());
    error = sysoprecover(&work, &cache, &failed, &pin);
  } while (error == -ERR_WOULD_BLOCK);
  CHECK(!error && pin == file && copied == IO_GUARD && outcome == -ERR_PEER &&
        !sysfilerefunpin(&pool, work.state.self, pin));
  pin = 0;
  if (mode == 2) {
    CHECK(sysoprecover(&work, &cache, &success, &pin) == -ERR_STATE && !pin &&
          !work.state.operations[success.lease.slot].status &&
          work.state.operations[success.lease.slot].words[2] == 8 &&
          !sysopcopy(&work, &cache, &success, 0, 8, &copied) && copied == IO_VALUE &&
          !sysopsaved(&work, &cache, &success) && !sysopack(&work, &cache, &success) &&
          !sysopfinish(&work, &cache, &success, &pin) && pin == file &&
          !sysfilerefunpin(&pool, work.state.self, pin));
  }
  CHECK(!sysfilerefclose(&pool, work.state.self, file, &anchor) &&
        !sysfilecloseended(&pool, &work, &cache, &anchor) &&
        !sysfilerefconnection(&pool, work.state.self, work.state.tag) &&
        !sysconnworkclean(&work, &cache));
  filecreatetrace[2] = 1;
  filecreatetrace[3] = copied == (mode == 2 ? IO_VALUE : IO_GUARD);
  filecreatetrace[4] = mode == 2;
  filecreatetrace[8] = steps;
  filecreatetrace[0] = 2;
}
static void
creatorbackendclient(const Bootview *boot, u32 mode)
{
  Opkey     key;
  Connpeer  peer;
  Conntag   tag;
  Connclaim claim;
  Connentry entry;
  Bootmap   map;
  Fileclose anchor;
  Handle    call, rpc, memory = 0, cached = 0;
  Handle    space = bootresource(boot, BOOT_SELF_SPACE);
  u64       file = 0, files[2], source = 0, steps = 0;
  Rkey      invocation;
  Msg       message;
  Ipcmsg    result;
  isize     error;
  setup(boot);
  CHECK(!sysctxinit(&contexts, work.state.self));
  key = begin(FS_OPEN, "/bound", 0);
  collect(&key);
  CHECK(!sysfilecreateack(&pool, &work, &cache, &key));
  peer = work.state.creators[key.lease.slot].peer;
  CHECK(!sysconnbind(&cache, peer, &tag) && !sysconnworkinit(&backend_work, &cache, tag) &&
        !sysfilecreateattachprepare(&pool, &work, &cache, &key, &backend_work, 0, &claim, &call));
  if (mode == 2) {
    bootstrap(&backend_work.state.bootstrap, claim, call, CALL_RECV_MEM);
    zero(&map, sizeof(map));
    CHECK(!sysbootmap(&backend_work.state.bootstrap, claim, space, ADDRESS + 0x10000, peer.image,
                      &map) &&
          !sysfilecreateattached(&pool, &work, &cache, &key, &backend_work, claim, space, &map, 0));
    zero(files, sizeof(files));
    CHECK(!sysfilecreatefinish(&pool, &work, &cache, &key, files, &source) && files[0] && !source);
    file = files[0];
    memory = backend_work.state.memory;
    cached = backend_work.state.call;
    key = begin(FS_OPEN, "/reuse", 0);
    collect(&key);
    CHECK(!sysfilecreateack(&pool, &work, &cache, &key) &&
          !sysfilecreateattachprepare(&pool, &work, &cache, &key, &backend_work, 0, &claim, &call));
  }
  CHECK(!sysctxbootcall(&contexts, work.state.self, 0, &backend_work.state.bootstrap, claim, call,
                        CALL_RECV_MEM, &invocation));
  if (!mode) {
    zero(&message, sizeof(message));
    message.words[0] = ENDPEER;
    CHECK(!ipc_call(work.state.call, &message, CALL_ASYNC | CALL_CONTROL, &result));
    rpc = result.request;
    CHECK(!ipc_rqwait(rpc, 0, &result) && !result.words[0] &&
          !ipc_rqack(rpc, ACK_RESULT | ACK_RELEASE));
  } else
    CHECK(!sysctxinvoke(&contexts, work.state.self, invocation) &&
          !sysctxhandoff(&contexts, work.state.self, invocation) &&
          !sysctxrewait(&contexts, work.state.self, invocation));
  CHECK(sysctxinvoke(&contexts, work.state.self, invocation) == -ERR_PEER &&
        !sysctxhandoff(&contexts, work.state.self, invocation) &&
        !sysctxbootpeerended(&contexts, work.state.self, invocation, &backend_work, &cache) &&
        !sysctxfinish(&contexts, work.state.self, invocation));
  if (mode) {
    CHECK(sysfilecreatebackendended(&pool, &work, &cache, &key, &backend_work, claim) ==
              -ERR_WOULD_BLOCK &&
          work.state.creators[key.lease.slot].call == call &&
          work.state.creators[key.lease.slot].token);
    filecreatetrace[5] = (u64) backend_work.state.bootstrap.rpc;
    filecreatetrace[6] = 10;
  }
  do {
    if (mode)
      CHECK(++steps < 64 && !__sys_yield());
    error = sysfilecreatebackendended(&pool, &work, &cache, &key, &backend_work, claim);
  } while (error == -ERR_WOULD_BLOCK);
  CHECK(!error && work.state.creators[key.lease.slot].disposition == SYSFILE_RESULT_ENDED &&
        !work.state.ended && backend_work.state.ended && __sys_free(call) == -ERR_BAD_HANDLE);
  zero(files, sizeof(files));
  CHECK(!sysfilecreatefinish(&pool, &work, &cache, &key, files, &source) && !files[0] && !source);
  zero(&map, sizeof(map));
  if (mode == 2) {
    CHECK(!sysconnworkbootended(&backend_work, &cache, claim) &&
          backend_work.state.memory == memory && backend_work.state.call == cached &&
          *(const volatile u64 *) (uintptr_t) backend_work.state.map.address == FS_CONN_MAGIC &&
          !sysfilerefdrop(&pool, work.state.self, file) &&
          !sysfilerefclose(&pool, work.state.self, file, &anchor) &&
          !sysfilecloseended(&pool, &backend_work, &cache, &anchor));
  } else
    CHECK(!sysconnworkdiscard(&backend_work, &cache, claim, space, &map));
  CHECK(!sysfilerefconnection(&pool, work.state.self, tag) &&
        !sysconnworkclean(&backend_work, &cache) && !sysconnview(&cache, work.state.tag, &entry) &&
        entry.state == SYSCONN_READY);
  zero(&message, sizeof(message));
  message.words[0] = STOP;
  CHECK(!ipc_call(work.state.call, &message, 0, &result) && !result.words[0] &&
        !sysconninvalidate(&cache, work.state.tag) && !sysconnworkclean(&work, &cache));
  filecreatetrace[2] = filecreatetrace[3] = 1;
  filecreatetrace[4] = mode == 2;
  filecreatetrace[8] = steps;
  filecreatetrace[0] = 2;
}
static void
bootpeerclient(const Bootview *boot, u32 mode)
{
  Connpeer  peer;
  Conntag   tag;
  Connclaim claim;
  Connentry entry;
  Bootmap   map;
  Handle    owned, redundant = 0, self, parent, memory = 0;
  Handle    space = bootresource(boot, BOOT_SELF_SPACE);
  u64       image, steps = 0;
  Rkey      invocation;
  isize     error;
  if (mode == 2) {
    setup(boot);
    tag = work.state.tag;
    memory = work.state.memory;
    CHECK(!sysconnreusebegin(&cache, tag, 202, &claim) &&
          !sysbootprepare(&work.state.bootstrap, claim, FS_ATTACH, 1, 0));
  } else {
    CHECK(!__sys_getid(&self, &image, &parent));
    peer.pid = self;
    peer.image = image;
    CHECK(!sysconninit(&cache, peer));
    owned = bootresource(boot, 256);
    CHECK(!sysconnpeer(owned, &peer) && !sysconnbind(&cache, peer, &tag) &&
          !sysconnworkinit(&work, &cache, tag) &&
          !sysconnworktakecall(&work, &cache, &owned, &redundant) && !owned && !redundant &&
          !sysconnbegin(&cache, tag, 1, &claim) &&
          !sysbootprepare(&work.state.bootstrap, claim, FS_HELLO, UINT64_MAX, 0));
  }
  CHECK(!sysctxinit(&contexts, work.state.self) &&
        !sysctxbootcall(&contexts, work.state.self, 0, &work.state.bootstrap, claim,
                        work.state.call, CALL_RECV_MEM, &invocation));
  if (!mode)
    while (!filecreatetrace[7])
      CHECK(!__sys_yield());
  else
    CHECK(!sysctxinvoke(&contexts, work.state.self, invocation) &&
          !sysctxhandoff(&contexts, work.state.self, invocation) &&
          !sysctxrewait(&contexts, work.state.self, invocation));
  CHECK(sysctxinvoke(&contexts, work.state.self, invocation) == -ERR_PEER &&
        !sysctxhandoff(&contexts, work.state.self, invocation) &&
        !sysctxbootpeerended(&contexts, work.state.self, invocation, &work, &cache) &&
        !sysctxfinish(&contexts, work.state.self, invocation) && work.state.ended == 1 &&
        !sysconnview(&cache, tag, &entry) && entry.state == SYSCONN_DRAINING);
  zero(&map, sizeof(map));
  if (mode) {
    error = mode == 2 ? sysconnworkbootended(&work, &cache, claim)
                      : sysconnworkdiscard(&work, &cache, claim, space, &map);
    CHECK(error == -ERR_WOULD_BLOCK && work.state.bootstrap.rpc && work.state.call &&
          (!memory || (work.state.memory == memory && work.state.map.mapped)));
    filecreatetrace[5] = (u64) work.state.bootstrap.rpc;
    filecreatetrace[6] = 9;
  }
  do {
    if (mode)
      CHECK(++steps < 64 && !__sys_yield());
    error = mode == 2 ? sysconnworkbootended(&work, &cache, claim)
                      : sysconnworkdiscard(&work, &cache, claim, space, &map);
  } while (error == -ERR_WOULD_BLOCK);
  CHECK(!error && !work.state.bootstrap.state && !sysconnview(&cache, tag, &entry) &&
        entry.state == SYSCONN_DRAINING && !entry.owner &&
        (!memory || (work.state.memory == memory && work.state.map.mapped &&
                     *(const volatile u64 *) ADDRESS == FS_CONN_MAGIC)) &&
        !sysconnworkclean(&work, &cache));
  filecreatetrace[1] = mode;
  filecreatetrace[2] = filecreatetrace[3] = 1;
  filecreatetrace[4] = steps;
  filecreatetrace[0] = 2;
}
static void
reuseabortclient(const Bootview *boot)
{
  Opkey     key;
  Connpeer  peer;
  Conntag   tag;
  Connentry entry;
  Connclaim attach, abort;
  Bootmap   mapped;
  Handle    call, memory, retained, extra, space = bootresource(boot, BOOT_SELF_SPACE);
  u64       files[2], source = 0, file, address;
  u32       variant;
  Msg       message;
  Ipcmsg    result;
  setup(boot);
  CHECK(!sysctxinit(&contexts, work.state.self));
  key = begin(FS_OPEN, "/bound", 0);
  collect(&key);
  CHECK(!sysfilecreateack(&pool, &work, &cache, &key));
  peer = work.state.creators[key.lease.slot].peer;
  CHECK(!sysconnbind(&cache, peer, &tag) && !sysconnworkinit(&backend_work, &cache, tag) &&
        !sysfilecreateattachprepare(&pool, &work, &cache, &key, &backend_work, 0, &attach, &call));
  bootstrap(&backend_work.state.bootstrap, attach, call, CALL_RECV_MEM);
  zero(&mapped, sizeof(mapped));
  CHECK(
      !sysbootmap(&backend_work.state.bootstrap, attach, space, ADDRESS + 0x10000, peer.image,
                  &mapped) &&
      !sysfilecreateattached(&pool, &work, &cache, &key, &backend_work, attach, space, &mapped, 0));
  zero(files, sizeof(files));
  CHECK(!sysfilecreatefinish(&pool, &work, &cache, &key, files, &source) && files[0] && !source);
  file = files[0];
  memory = backend_work.state.memory;
  retained = backend_work.state.call;
  address = backend_work.state.map.address;
  for (variant = 0; variant < 2; variant++) {
    key = begin(FS_OPEN, "/reuse", 0);
    collect(&key);
    CHECK(
        !sysfilecreateack(&pool, &work, &cache, &key) &&
        !sysfilecreateattachprepare(&pool, &work, &cache, &key, &backend_work, 0, &attach, &call));
    bootstrap(&backend_work.state.bootstrap, attach, call, CALL_RECV_MEM);
    extra = backend_work.state.bootstrap.resource;
    CHECK((!variant && !extra && sysconnworkreuse(&backend_work, &cache, attach) == -ERR_PEER) ||
          (variant && extra && extra != memory &&
           sysconnworkreuse(&backend_work, &cache, attach) == -ERR_INVALID));
    CHECK(!sysfilecreaterevokeprepare(&pool, &work, &cache, &key, 0, 0, &abort, &call));
    bootstrap(&work.state.bootstrap, abort, call, CALL_CONTROL);
    CHECK(!sysfilecreaterevoked(&pool, &work, &cache, &key, abort));
    zero(files, sizeof(files));
    CHECK(!sysfilecreatefinish(&pool, &work, &cache, &key, files, &source) && !files[0] &&
          !source && !sysconnworkreuseabort(&backend_work, &cache, attach) &&
          !backend_work.state.bootstrap.state && !backend_work.state.ended &&
          backend_work.state.memory == memory && backend_work.state.call == retained &&
          backend_work.state.map.address == address && backend_work.state.map.verified &&
          *(const volatile u64 *) (uintptr_t) address == FS_CONN_MAGIC &&
          !sysconnview(&cache, tag, &entry) && entry.state == SYSCONN_READY && !entry.owner &&
          entry.connid == CONNID && (!extra || __sys_free(extra) == -ERR_BAD_HANDLE));
  }
  closeat(&backend_work, file, 1);
  zero(&message, sizeof(message));
  message.words[0] = STOP;
  CHECK(!ipc_call(retained, &message, 0, &result) && result.words[0] == 3 && result.words[1] == 3 &&
        !sysconninvalidate(&cache, tag) && !sysconnworkclean(&backend_work, &cache) &&
        !ipc_call(work.state.call, &message, 0, &result) && !result.words[0] &&
        !sysconninvalidate(&cache, work.state.tag) && !sysconnworkclean(&work, &cache));
  filecreatetrace[2] = 2;
  filecreatetrace[3] = filecreatetrace[4] = 1;
  filecreatetrace[0] = 2;
}
static void
mapdiscardclient(const Bootview *boot)
{
  Opkey     key;
  Connpeer  peer, actual;
  Conntag   tag;
  Connentry entry;
  Connclaim attach, abort, retry;
  Bootmap   mapped;
  Handle    call, memory, retained, space = bootresource(boot, BOOT_SELF_SPACE);
  u64       files[2], source = 0, copied;
  Msg       message;
  Ipcmsg    result;
  setup(boot);
  CHECK(!sysctxinit(&contexts, work.state.self));
  key = begin(FS_OPEN, "/bound", 0);
  collect(&key);
  CHECK(!sysfilecreateack(&pool, &work, &cache, &key));
  peer = work.state.creators[key.lease.slot].peer;
  CHECK(!sysconnbind(&cache, peer, &tag) && !sysconnworkinit(&backend_work, &cache, tag) &&
        !sysfilecreateattachprepare(&pool, &work, &cache, &key, &backend_work, 0, &attach, &call));
  bootstrap(&backend_work.state.bootstrap, attach, call, CALL_RECV_MEM);
  memory = backend_work.state.bootstrap.resource;
  zero(&mapped, sizeof(mapped));
  CHECK(sysbootmap(&backend_work.state.bootstrap, attach, space, ADDRESS + 0x10000, peer.image,
                   &mapped) == -ERR_INVALID &&
        mapped.mapped && !mapped.verified &&
        !sysfilecreatekeepcall(&pool, &work, &cache, &key, &backend_work, attach));
  retained = backend_work.state.call;
  CHECK(retained &&
        !sysfilecreaterevokeprepare(&pool, &work, &cache, &key, retained, 0, &abort, &call));
  bootstrap(&work.state.bootstrap, abort, call, CALL_CONTROL);
  CHECK(!sysfilecreaterevoked(&pool, &work, &cache, &key, abort));
  zero(files, sizeof(files));
  CHECK(!sysfilecreatefinish(&pool, &work, &cache, &key, files, &source) && !files[0] && !source &&
        backend_work.state.bootstrap.resource == memory && backend_work.state.call == retained &&
        !sysconnworkdiscard(&backend_work, &cache, attach, space, &mapped) && !mapped.mapped &&
        !backend_work.state.bootstrap.state && !backend_work.state.ended &&
        backend_work.state.call == retained && !sysconnview(&cache, tag, &entry) &&
        entry.state == SYSCONN_DRAINING && !entry.owner &&
        sysconnbegin(&cache, tag, 999, &retry) == -ERR_PEER &&
        __sys_copy(&copied, (const void *) (ADDRESS + 0x10000), sizeof(copied)) == -ERR_FAULT &&
        __sys_free(memory) == -ERR_BAD_HANDLE && !sysconnpeer(retained, &actual) &&
        actual.pid == peer.pid && actual.image == peer.image);
  zero(&message, sizeof(message));
  message.words[0] = STOP;
  CHECK(!ipc_call(retained, &message, 0, &result) && result.words[0] == 1 && result.words[1] == 1 &&
        !sysconnworkclean(&backend_work, &cache) &&
        !ipc_call(work.state.call, &message, 0, &result) && !result.words[0] &&
        !sysconninvalidate(&cache, work.state.tag) && !sysconnworkclean(&work, &cache));
  filecreatetrace[2] = filecreatetrace[3] = 1;
  filecreatetrace[0] = 2;
}
static void
mapsalvageclient(const Bootview *boot)
{
  Opkey     key;
  Connpeer  peer;
  Conntag   tag;
  Connclaim attach, abort;
  Bootmap   mapped;
  Allocarg  allocation;
  Maparg    guard;
  Handle    call, blocker, memory, retained;
  u64       files[2], source = 0, file;
  Msg       message;
  Ipcmsg    result;
  Handle    space = bootresource(boot, BOOT_SELF_SPACE);
  setup(boot);
  CHECK(!sysctxinit(&contexts, work.state.self));
  key = begin(FS_OPEN, "/bound", 0);
  collect(&key);
  CHECK(!sysfilecreateack(&pool, &work, &cache, &key));
  peer = work.state.creators[key.lease.slot].peer;
  CHECK(!sysconnbind(&cache, peer, &tag) && !sysconnworkinit(&backend_work, &cache, tag) &&
        !sysfilecreateattachprepare(&pool, &work, &cache, &key, &backend_work, 0, &attach, &call));
  bootstrap(&backend_work.state.bootstrap, attach, call, CALL_RECV_MEM);
  memory = backend_work.state.bootstrap.resource;
  zero(&allocation, sizeof(allocation));
  allocation.bytes = PAGE_SIZE;
  allocation.kind = ALLOC_DATA;
  CHECK(!__sys_alloc(bootresource(boot, BOOT_SELF_ALLOC), &allocation, &blocker));
  zero(&guard, sizeof(guard));
  guard.mem = blocker;
  guard.address = ADDRESS + 0x10000;
  guard.length = PAGE_SIZE;
  guard.prot = 3;
  CHECK(!__sys_map(space, &guard));
  *(volatile u64 *) (uintptr_t) guard.address = 12345;
  zero(&mapped, sizeof(mapped));
  CHECK(sysbootmap(&backend_work.state.bootstrap, attach, space, guard.address, peer.image,
                   &mapped) == -ERR_STATE &&
        !mapped.mapped && backend_work.state.bootstrap.resource == memory &&
        *(volatile u64 *) (uintptr_t) guard.address == 12345 &&
        !sysfilecreatekeepcall(&pool, &work, &cache, &key, &backend_work, attach));
  retained = backend_work.state.call;
  CHECK(retained && !work.state.creators[key.lease.slot].call &&
        !sysfilecreaterevokeprepare(&pool, &work, &cache, &key, retained, 0, &abort, &call));
  bootstrap(&work.state.bootstrap, abort, call, CALL_CONTROL);
  CHECK(!sysfilecreaterevoked(&pool, &work, &cache, &key, abort));
  zero(files, sizeof(files));
  CHECK(!sysfilecreatefinish(&pool, &work, &cache, &key, files, &source) && !files[0] && !source &&
        backend_work.state.bootstrap.resource == memory && backend_work.state.call == retained);
  CHECK(!__sys_unmap(space, guard.address) && !__sys_free(blocker) &&
        !sysbootmap(&backend_work.state.bootstrap, attach, space, guard.address, peer.image,
                    &mapped) &&
        !sysconnworkadopt(&backend_work, &cache, attach, space, &mapped) &&
        !sysbootfinish(&backend_work.state.bootstrap, attach));
  key = begin(FS_OPEN, "/reuse", 0);
  collect(&key);
  CHECK(!sysfilecreateack(&pool, &work, &cache, &key) &&
        !sysfilecreateattachprepare(&pool, &work, &cache, &key, &backend_work, 0, &attach, &call));
  bootstrap(&backend_work.state.bootstrap, attach, call, CALL_RECV_MEM);
  zero(&mapped, sizeof(mapped));
  CHECK(
      !sysfilecreateattached(&pool, &work, &cache, &key, &backend_work, attach, space, &mapped, 0));
  zero(files, sizeof(files));
  CHECK(!sysfilecreatefinish(&pool, &work, &cache, &key, files, &source) && files[0] && !source &&
        backend_work.state.memory == memory && backend_work.state.call == retained);
  file = files[0];
  closeat(&backend_work, file, 1);
  message.words[0] = STOP;
  message.words[1] = message.words[2] = message.words[3] = 0;
  CHECK(!ipc_call(backend_work.state.call, &message, 0, &result) && result.words[0] == 2 &&
        result.words[1] == 2 && !sysconninvalidate(&cache, backend_work.state.tag) &&
        !sysconnworkclean(&backend_work, &cache));
  CHECK(!ipc_call(work.state.call, &message, 0, &result) && !result.words[0] &&
        !sysconninvalidate(&cache, work.state.tag) && !sysconnworkclean(&work, &cache));
  filecreatetrace[2] = 2;
  filecreatetrace[3] = 1;
  filecreatetrace[0] = 2;
}
int
main(int argc, char **argv)
{
  CHECK(argc == 1 &&
        ((argv[0][0] >= '0' && argv[0][0] <= '9') || (argv[0][0] >= 'a' && argv[0][0] <= 'z') ||
         (argv[0][0] >= 'A' && argv[0][0] <= 'Q')) &&
        !argv[0][1]);
  filecreatetrace[0] = 1;
  if (argv[0][0] == '0')
    client(sysboot());
  else if (argv[0][0] == '1')
    server(sysboot(), 0);
  else if (argv[0][0] == '2')
    backend(sysboot(), 0);
  else if (argv[0][0] == '3')
    deadclient(sysboot());
  else if (argv[0][0] == '4')
    deadserver(sysboot(), 0);
  else if (argv[0][0] == '5' || argv[0][0] == '7')
    createdeadclient(sysboot(), argv[0][0] == '5' ? FS_OPEN : FS_PIPE);
  else if (argv[0][0] == '6' || argv[0][0] == '8')
    deadserver(sysboot(), argv[0][0] == '6' ? FS_OPEN : FS_PIPE);
  else if (argv[0][0] == '9')
    resourceendclient(sysboot(), 0);
  else if (argv[0][0] == 'a')
    server(sysboot(), 1);
  else if (argv[0][0] == 'b')
    backend(sysboot(), 1);
  else if (argv[0][0] == 'c')
    mapsalvageclient(sysboot());
  else if (argv[0][0] == 'd')
    server(sysboot(), 2);
  else if (argv[0][0] == 'e')
    backend(sysboot(), 2);
  else if (argv[0][0] == 'f')
    resourceendclient(sysboot(), 1);
  else if (argv[0][0] == 'g')
    server(sysboot(), 3);
  else if (argv[0][0] == 'h')
    backend(sysboot(), 3);
  else if (argv[0][0] == 'i')
    mapdiscardclient(sysboot());
  else if (argv[0][0] == 'j')
    server(sysboot(), 4);
  else if (argv[0][0] == 'k')
    backend(sysboot(), 4);
  else if (argv[0][0] == 'l')
    reuseabortclient(sysboot());
  else if (argv[0][0] == 'm')
    server(sysboot(), 5);
  else if (argv[0][0] == 'n')
    backend(sysboot(), 5);
  else if (argv[0][0] >= 'o' && argv[0][0] <= 'q')
    bootpeerclient(sysboot(), (u32) (argv[0][0] - 'o'));
  else if (argv[0][0] == 'r')
    filecreatetrace[0] = 2;
  else if (argv[0][0] == 's' || argv[0][0] == 't')
    deadserver(sysboot(), argv[0][0] == 's' ? FS_HELLO : FS_ATTACH);
  else if (argv[0][0] == 'u' || argv[0][0] == 'x' || argv[0][0] == 'A')
    creatorbackendclient(sysboot(), argv[0][0] == 'u' ? 0 : argv[0][0] == 'x' ? 1 : 2);
  else if (argv[0][0] == 'v' || argv[0][0] == 'y' || argv[0][0] == 'B')
    server(sysboot(), argv[0][0] == 'v' ? 6 : argv[0][0] == 'y' ? 7 : 8);
  else if (argv[0][0] == 'D' || argv[0][0] == 'F' || argv[0][0] == 'H')
    iorecoverclient(sysboot(), argv[0][0] == 'D' ? 0 : argv[0][0] == 'F' ? 1 : 2);
  else if (argv[0][0] == 'E' || argv[0][0] == 'G' || argv[0][0] == 'I')
    server(sysboot(), argv[0][0] == 'E' ? 9 : argv[0][0] == 'G' ? 10 : 11);
  else if (argv[0][0] == 'J')
    exitclient(sysboot());
  else if (argv[0][0] == 'K')
    server(sysboot(), 12);
  else if (argv[0][0] == 'L')
    vfsdeathclient(sysboot(), 0);
  else if (argv[0][0] == 'M')
    server(sysboot(), 13);
  else if (argv[0][0] == 'N')
    backend(sysboot(), 9);
  else if (argv[0][0] == 'O')
    vfsdeathclient(sysboot(), 1);
  else if (argv[0][0] == 'P')
    server(sysboot(), 14);
  else if (argv[0][0] == 'Q')
    backend(sysboot(), 10);
  else
    backend(sysboot(), argv[0][0] == 'w' ? 6 : argv[0][0] == 'z' ? 7 : 8);
  return 42;
}
