#include <sys/boot.h>
#include <sys/channel.h>
#include "private/syscall.h"
#include "private/start.h"
#include "private/filecreate.h"
#include "private/close.h"
#include "private/fd.h"
#include "private/contextipc.h"
#include <abi/resource.h>

#define ADDRESS 0x1600000UL
#define STOP    0xf001
#define GATE    0xf002
#define PREPARE 0xf003
#define CONNID  0x10001
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
  bound = files[0];
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
creatorhandler(u64 signum)
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
static NORETURN void
creatortrampoline(u64 signum, const Siginfo *info, void *trap, u64 function, u64 address, u64 flags)
{
  u64 mask, ignored;
  CHECK(signum == SIGNAL_USR1 && !info->code && !flags && function == (u64) creatorhandler &&
        !__sys_sigmask(SIGNAL_BLOCK, SIGNAL_SUPPORTED, &mask) &&
        !sysctxsignal(&contexts, work.state.self, active_key, trap, 0) &&
        !__sys_sigmask(SIGNAL_SETMASK, mask, &ignored));
  creatorhandler(signum);
  __sys_sigreturn((void *) address);
}
static void
creatorsignals(void)
{
  Sigact action;
  CHECK(!sysctxinit(&contexts, work.state.self));
  zero(&action, sizeof(action));
  action.kind = SIGNAL_HANDLER;
  action.handler = (u64) creatorhandler;
  action.trampoline = (u64) creatortrampoline;
  CHECK(!__sys_sigaction(SIGNAL_USR1, &action, NULL));
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
  u64       slot, seq, opcode, i, found, number, resourcepath;
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
      CHECK(!pending && created == 7 && closed == 7);
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
      resourcepath = 0;
      if (opcode == FS_OPEN) {
        const volatile char *path = (const volatile char *) data;
        CHECK(!descriptor.object && !descriptor.args[2] &&
              descriptor.args[3] == descriptor.payload_bytes && path[0] == '/');
        if (path[1] == 'm') {
          reply.words[0] = (u64) -2;
          CHECK(!ipc_reply(input.request, &reply));
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
        CHECK(!ipc_reply_resource(input.request, &transfer));
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
static void
backend(const Bootview *boot)
{
  Connpeer  clientpeer, vfspeer;
  Ipcmsg    input;
  Msg       reply;
  Resreply  transfer;
  Allocarg  alloc;
  Maparg    mapping;
  Fileslot  descriptor;
  Handle    memory = 0, space = bootresource(boot, BOOT_SELF_SPACE);
  Filehead *head = (Filehead *) ADDRESS;
  u64       sessions[4], references[4], created = 0, closed = 0, token, i;
  CHECK(!sysconnpeer(bootresource(boot, 256), &clientpeer) &&
        !sysconnpeer(bootresource(boot, 257), &vfspeer));
  zero(sessions, sizeof(sessions));
  zero(references, sizeof(references));
  for (;;) {
    CHECK(!ipc_recv(0, 0, &input) && input.kind == IPC_CALL);
    zero(&reply, sizeof(reply));
    if (input.words[0] == PREPARE) {
      CHECK(input.source == vfspeer.pid && input.image == vfspeer.image && created < 4 &&
            input.words[3] == created + 1 && input.words[1] && input.words[2]);
      sessions[created] = input.words[1];
      references[created] = input.words[2];
      created++;
      reply.words[1] = input.words[3];
      CHECK(!ipc_reply(input.request, &reply));
      continue;
    }
    CHECK(input.source == clientpeer.pid && input.image == clientpeer.image);
    if (input.words[0] == STOP) {
      CHECK(created == 4 && closed == 4 && filecreatetrace[3] == 3 && filecreatetrace[4] == 3);
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
      reply.words[1] = input.words[2];
      if (input.words[0] == FS_ABORT) {
        if (references[token - 1]) {
          references[token - 1] = 0;
          closed++;
        }
        filecreatetrace[4]++;
        CHECK(!ipc_reply(input.request, &reply));
        continue;
      }
      CHECK(references[token - 1]);
      filecreatetrace[3]++;
      if (!memory) {
        Handle pid, parent;
        u64    image;
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
      } else {
        reply.words[2] = CONNID;
        reply.words[3] = FS_CONN_BYTES;
        CHECK(!ipc_reply(input.request, &reply));
      }
      continue;
    }
    CHECK(input.words[0] == FS_CLOSE && input.words[1] == CONNID && input.words[3] == 6 &&
          !sysfilerequest(head, FS_CONN_BYTES, 6, input.words[2], &descriptor) &&
          descriptor.span == 1 && !descriptor.payload_bytes);
    i = 0;
    while (i < created && (sessions[i] != descriptor.object || references[i] != descriptor.args[0]))
      i++;
    CHECK(i < created && references[i]);
    references[i] = 0;
    closed++;
    reply.words[1] = input.words[2];
    CHECK(!ipc_reply(input.request, &reply));
  }
  CHECK(memory && !__sys_unmap(space, ADDRESS) && !__sys_free(memory) &&
        !__sys_free(bootresource(boot, 256)) && !__sys_free(bootresource(boot, 257)));
  filecreatetrace[1] = created;
  filecreatetrace[2] = closed;
  filecreatetrace[0] = 2;
}
int
main(int argc, char **argv)
{
  CHECK(argc == 1 && argv[0][0] >= '0' && argv[0][0] <= '2' && !argv[0][1]);
  filecreatetrace[0] = 1;
  if (argv[0][0] == '0')
    client(sysboot());
  else if (argv[0][0] == '1')
    server(sysboot());
  else
    backend(sysboot());
  return 42;
}
