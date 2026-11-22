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
#include "private/execsend.h"
#include "private/execmanifest.h"
#include "private/elf.h"
#include "private/contextipc.h"
#include "private/sigentry.h"
#include "private/serial.h"
#include <abi/resource.h>
#include <abi/observe.h>
#include "../../sbin/tty/serial.h"
#include "../../sbin/fs/loadio.h"
#include "../../sbin/fs/cache.h"
#include "execclaimuser.h"

#define ADDRESS   0x1600000UL
#define STOP      0xf001
#define GATE      0xf002
#define PREPARE   0xf003
#define ENDPEER   0xf004
#define SEEN      0xf005
#define LOADTEST  0xf006
#define EXECWATCH 0xf007
#define CONNID    0x10001
#define IO_VALUE  UINT64_C(0x1122334455667788)
#define IO_WRITE  UINT64_C(0x8877665544332211)
#define IO_GHOST  UINT64_C(0xdeadbeef5a5a5a5a)
#define IO_GUARD  UINT64_C(0x2468ace013579bdf)
static Connwork connections[SYSCONN_COUNT];
#define work         (connections[0])
#define backend_work (connections[1])
#define cache        (*(Conncache *) (void *) work.reserved)
static Filepool       pool;
static Fdtable        fds;
static Execfds        execfds;
static Execsend       execsend;
static Execmanifest   execplan;
static Execreserve    execreserve;
static Rbank          contexts;
static Rkey           active_key;
static Ttyinput       ttyline;
static Serialring     ttyrx, ttytx;
static Loadtable      loadtable;
static const Loadfile loadfile = {7, 2, 3, (LOAD_DIRECT + 1) * CACHE_BYTES};
static const Loadmap  loadmap = {
    {7, 2, 3, (LOAD_DIRECT + 1) * CACHE_BYTES}, {99, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 100}, 102};
static const Loadindex loadindex = {102, {101}};
static Cacheblock      loadblocks[2];
volatile u64           filecreatetrace[16];
extern const u8        execclaim_elf[], execclaim_elf_end[];
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
setupwithhello(const Bootview *boot, u64 seq)
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
        !sysconnbegin(&cache, tag, seq, &claim) &&
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
static void
setup(const Bootview *boot)
{
  setupwithhello(boot, 1);
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
  Opkey   key;
  Conntag tag;
  Bootmap mapped = {0};
  Rkey    active = {0};
  Handle  space = bootresource(boot, BOOT_SELF_SPACE), rpc;
  u64     sequence;
  u64     files[2], source = 0;
  key = begin(FS_OPEN, "/bound", 0);
  collect(&key);
  CHECK(sysfilecreatebackend(&pool, &work, &cache, &key, &tag) == -ERR_STATE &&
        !sysfilecreateack(&pool, &work, &cache, &key));
  CHECK(sysctxopenconnect(&contexts, 0, &pool, connections, &cache, &key, space, ADDRESS, 0,
                          &mapped, &active) < 0 &&
        !mapped.mapped && !active.sequence && backend_work.state.bootstrap.state == SYSBOOT_RESULT);
  tag = backend_work.state.tag;
  rpc = backend_work.state.bootstrap.rpc;
  sequence = backend_work.state.bootstrap.claim.seq;
  CHECK(rpc &&
        !sysctxopenconnect(&contexts, 0, &pool, connections, &cache, &key, space, ADDRESS + 0x10000,
                           0, &mapped, &active) &&
        backend_work.state.bootstrap.state == SYSBOOT_FREE && backend_work.state.map.verified &&
        cache.entries[tag.index].sequence == sequence);
  zero(files, sizeof(files));
  CHECK(!sysfilecreatefinish(&pool, &work, &cache, &key, files, &source) && files[0] && !source);
  return files[0];
}
static void
resourceopens(const Bootview *boot)
{
  Opencreate open = {0};
  Rkey       active = {0};
  Fdview     fd;
  int        number = -2;
  isize      business = 9;
  Opkey      key;
  Conntag    tag;
  Connclaim  claim;
  Connpeer   peer;
  Bootmap    mapped;
  Handle     call;
  u64        bound, reused, token;
  u32        i, reserved;
  bound = attachfirst(boot);
  CHECK(!sysconnpeer(backend_work.state.call, &peer));
  CHECK(sysctxopencreate(&contexts, 0, &pool, &work, &cache, &fds, "/reuse", 0, 0, BOOT_FD_CLOEXEC,
                         &open, &active, &number, &business) == 1 &&
        number == -2 && business == 9 && !active.sequence &&
        !sysfdobserve(&fds, open.tag.index, &fd) && fd.tag.state == SYSFD_RESERVED);
  filecreatetrace[1]++;
  key = open.operation;
  tag = backend_work.state.tag;
  CHECK(!sysconnreusebegin(&cache, tag, UINT64_MAX, &claim) &&
        sysfilecreatebackend(&pool, &work, &cache, &key, &tag) == -ERR_BUSY &&
        !sysconnrollback(&cache, claim) &&
        !sysfilecreatebackend(&pool, &work, &cache, &key, &tag) &&
        tag.generation == backend_work.state.tag.generation &&
        tag.index == backend_work.state.tag.index);
  zero(&mapped, sizeof(mapped));
  CHECK(work.state.operations[key.lease.slot].phase == SYSOP_ACKED &&
        !sysctxopenconnect(&contexts, 0, &pool, connections, &cache, &key,
                           bootresource(boot, BOOT_SELF_SPACE), 0, 0, &mapped, &active) &&
        !mapped.mapped && !mapped.verified);
  number = (int) sysfileopenfinish(&pool, &work, &cache, &fds, &open, &business);
  CHECK(number >= 0 && !business && !sysfdobserve(&fds, (u32) number, &fd) &&
        fd.flags == BOOT_FD_CLOEXEC && !sysfddetach(&fds, fd.tag, &fd));
  reused = fd.file;
  for (i = 0; i < SYSCONN_COUNT - 2; i++) {
    peer.image++;
    CHECK(!sysconnbind(&cache, peer, &tag));
  }
  peer.image++;
  CHECK(sysconnbind(&cache, peer, &tag) == -ERR_CAPACITY);
  for (i = 0; i < 2; i++) {
    CHECK(sysctxopencreate(&contexts, 0, &pool, &work, &cache, &fds, i ? "/failed" : "/cancel", 0,
                           0, 0, &open, &active, &number, &business) == 1);
    filecreatetrace[1]++;
    key = open.operation;
    reserved = open.tag.index;
    token = work.state.creators[key.lease.slot].token;
    zero(&mapped, sizeof(mapped));
    if (i) {
      CHECK(
          !sysfilecreateattachprepare(&pool, &work, &cache, &key, &backend_work, 0, &claim, &call));
      bootstrap(&backend_work.state.bootstrap, claim, call, CALL_RECV_MEM);
      mapped.address = ADDRESS;
      CHECK(sysfilecreateattached(&pool, &work, &cache, &key, &backend_work, claim,
                                  bootresource(boot, BOOT_SELF_SPACE), &mapped, 0) == -ERR_INVALID);
      zero(&mapped, sizeof(mapped));
      CHECK(!sysfilecreaterevokeprepare(&pool, &work, &cache, &key, 0, 0, &claim, &call) &&
            !sysctxbootcall(&contexts, work.state.self, 0, &work.state.bootstrap, claim, call, 0,
                            &active));
    }
    CHECK(sysfileopendiscard(&pool, &work, &cache, &fds, &open) == -ERR_STATE &&
          !sysfdobserve(&fds, reserved, &fd) && fd.tag.state == SYSFD_RESERVED &&
          !sysctxopencancel(&contexts, 0, &pool, connections, &cache, &fds, &open,
                            bootresource(boot, BOOT_SELF_SPACE), &mapped, &active) &&
          !active.sequence && work.state.bootstrap.state == SYSBOOT_FREE &&
          backend_work.state.bootstrap.state == SYSBOOT_FREE && !open.operation.lease.seq &&
          !sysfdobserve(&fds, reserved, &fd) && fd.tag.state == SYSFD_FREE);
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
          !sysfileconnclean(&pool, &backend_work, &cache));
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
ttyiocall(u64 file, u64 opcode, u32 cancel)
{
  Ipcmsg       result;
  Filelease    lease;
  Fileslot     slot = {0};
  Fileref      ref;
  Msg          call = {{opcode, work.state.map.connid, 0, 0}}, seen = {{SEEN, 0, 0, 0}};
  Handle       rpc;
  volatile u8 *page;
  CHECK(!sysfilerefview(&pool, work.state.self, file, &ref) &&
        !sysconnworkreserve(&work, &cache, SYSFILE_MAIN, 1, &lease));
  slot.seq = call.words[2] = lease.seq;
  call.words[3] = lease.slot;
  slot.state = FS_SLOT_SUBMITTED;
  slot.span = 1;
  slot.object = ref.session;
  slot.payload_bytes = slot.args[1] = 2;
  page = (volatile u8 *) (uintptr_t) (work.state.map.address + FS_DATA_OFFSET +
                                      lease.slot * FS_SLOT_BYTES);
  page[0] = page[1] = 0xa5;
  if (opcode == FS_WRITE) {
    page[0] = cancel ? 'x' : 'b';
    page[1] = '\n';
  }
  if (cancel == 2)
    CHECK(!ipc_call(work.state.call, &seen, CALL_CONTROL, &result) && !result.words[0]);
  CHECK(!__sys_copy((void *) (uintptr_t) (work.state.map.address + FS_DESC_OFFSET +
                                          lease.slot * FS_DESC_BYTES),
                    &slot, sizeof(slot)) &&
        !ipc_call(work.state.call, &call, CALL_ASYNC, &result));
  rpc = result.request;
  if (cancel == 1) {
    CHECK(!ipc_call(work.state.call, &seen, CALL_CONTROL, &result) && !result.words[0]);
    ttyiocall(file, opcode, 4);
    CHECK(!ipc_rqcancel(rpc) && !ipc_call(work.state.call, &seen, CALL_CONTROL, &result) &&
          !result.words[0]);
  }
  if (cancel == 5)
    CHECK(!ipc_call(work.state.call, &seen, CALL_CONTROL, &result) && !result.words[0] &&
          !ipc_rqcancel(rpc));
  CHECK(!ipc_rqwait(rpc, 0, &result) && result.words[1] == lease.seq);
  if (cancel == 4)
    CHECK((isize) result.words[0] == -EAGAIN && !result.words[2] && !result.words[3] &&
          page[0] == (opcode == FS_WRITE ? 'x' : 0xa5) &&
          page[1] == (opcode == FS_WRITE ? '\n' : 0xa5));
  else if (cancel == 2)
    CHECK(!result.words[0] && !result.words[2] && !result.words[3] && page[0] == 0xa5 &&
          page[1] == 0xa5);
  else if (cancel == 3)
    CHECK(!result.words[0] && result.words[2] == 1 && result.words[3] == 1 && page[0] == 'a' &&
          page[1] == 0xa5);
  else if (cancel)
    CHECK((isize) result.words[0] == -EINTR && !result.words[2] && !result.words[3] &&
          page[0] == (opcode == FS_WRITE ? 'x' : 0xa5) &&
          page[1] == (opcode == FS_WRITE ? '\n' : 0xa5));
  else if (opcode == FS_READ)
    CHECK(!result.words[0] && result.words[2] == 2 && result.words[3] == 2 && page[0] == 'b' &&
          page[1] == '\n');
  else
    CHECK(!result.words[0] && result.words[2] == 2 && !result.words[3]);
  CHECK(!ipc_rqwait(rpc, RQ_FINISH, &result) && !ipc_rqack(rpc, ACK_RESULT | ACK_RELEASE) &&
        !sysconnworkrelease(&work, &cache, lease));
}

static void
execmanifest(const Bootview *boot, u64 file, u32 count)
{
  Fdtag     tag;
  Fdview    fd;
  Filelease spare;
  Msg       message;
  Ipcmsg    result;
  u64       pin = file, closing = 0;
  Handle    rpc, original, view = 0;
  u64       target = work.state.self.image + 1;
  u32       i;
  if (count > 1)
    CHECK(!__sys_execprep(bootresource(boot, BOOT_SELF_SPACE), 0, &view, &target));
  for (i = 0; i < count; i++)
    CHECK(!sysfilerefalias(&pool, work.state.self, file) && !sysfdreserve(&fds, 0, &tag) &&
          tag.index == i && !sysfdpublish(&fds, tag, file, i & 1 ? 0 : BOOT_FD_CLOEXEC));
  if (count > 1) {
    volatile Fileslot *next =
        (volatile Fileslot *) (work.state.map.address + FS_DESC_OFFSET + FS_DESC_BYTES);
    next->seq = UINT64_MAX;
  }
  CHECK(!sysexecfds(&execfds, &fds, &pool, &cache, work.state.self) &&
        !sysfilerefpin(&pool, work.state.self, pin) &&
        !sysexecfdbegin(&execfds, &work, &cache, SYSFILE_MAIN, 1, target, 100, &pin, &execsend) &&
        !pin && execsend.key.lease.span == (count > 1 ? 2U : 1U));
  if (count > 1)
    CHECK(sysconnworkreserve(&work, &cache, SYSFILE_MAIN, 1, &spare) == -ERR_BUSY);
  CHECK(!sysoprequest(&work, &cache, &execsend.key, &message) &&
        !ipc_call(work.state.call, &message, CALL_ASYNC, &result));
  rpc = original = result.request;
  CHECK(
      !sysopsubmitted(&work, &cache, &execsend.key, &rpc) && !rpc &&
      sysoprequest(&work, &cache, &execsend.key, &message) == -ERR_STATE &&
      !ipc_rqwait(original, 0, &result) && (isize) result.words[0] == (count == 1 ? -ENOTSUP : 0) &&
      result.words[1] == execsend.key.lease.seq && result.words[2] == (count == 1 ? 0U : 41U) &&
      !result.words[3] && !sysopcapture(&work, &cache, &execsend.key, 0, &result) &&
      !sysopfinished(&work, &cache, &execsend.key) && !sysopsaved(&work, &cache, &execsend.key) &&
      !sysopack(&work, &cache, &execsend.key) && !sysopfinish(&work, &cache, &execsend.key, &pin) &&
      pin == file && !sysfilerefunpin(&pool, work.state.self, pin) && !sysexecfdrelease(&execfds));
  if (count > 1)
    CHECK(!sysconnworkreserve(&work, &cache, SYSFILE_MAIN, 2, &spare) &&
          !sysconnworkrelease(&work, &cache, spare));
  for (i = 0; i < count; i++)
    CHECK(!sysfdobserve(&fds, i, &fd) &&
          !sysfdclose(&fds, &pool, work.state.self, fd.tag, &closing) && !closing);
  execsend.key.lease.seq = 0;
  if (view) {
    Msg check = {{EXECWATCH, 0, 0, 0}};
    Msg claim = {{FS_EXEC_CLAIM, 0, 1, 41}};
    CHECK(!ipc_call(work.state.call, &claim, CALL_RECV_MEM, &result) &&
          (isize) result.words[0] == -ESTALE && result.words[1] == claim.words[2]);
    CHECK(!__sys_execabort(view) && !ipc_call(work.state.call, &check, CALL_CONTROL, &result) &&
          !result.words[0]);
  }
  filecreatetrace[14] += count;
}

static void
loadreads(void)
{
  Msg          control = {{LOADTEST, 0, 0, 0}}, call = {{FS_LOAD, work.state.map.connid, 0, 0}};
  Ipcmsg       result;
  Filelease    lease;
  Fileslot     slot = {0};
  Handle       rpc;
  volatile u8 *page;
  u32          attempt, i;
  CHECK(!ipc_call(work.state.call, &control, CALL_CONTROL, &result) && result.words[2]);
  slot.object = result.words[2];
  slot.state = FS_SLOT_SUBMITTED;
  slot.span = 1;
  slot.payload_bytes = FS_SLOT_BYTES;
  slot.args[0] = loadfile.inode;
  slot.args[1] = loadfile.generation;
  slot.args[2] = loadfile.version;
  slot.args[3] = loadfile.length;
  slot.args[5] = 4;
  slot.args[6] = FS_SLOT_BYTES - 4;
  for (attempt = 0; attempt < 5; attempt++) {
    if (attempt >= 3) {
      control.words[1] = attempt == 3 ? 6 : 7;
      CHECK(!ipc_call(work.state.call, &control, CALL_CONTROL, &result));
    }
    CHECK(!sysconnworkreserve(&work, &cache, SYSFILE_MAIN, 1, &lease));
    slot.seq = call.words[2] = lease.seq;
    call.words[3] = lease.slot;
    slot.args[4] = attempt >= 3 ? 0 : attempt == 1 ? 4 : LOAD_DIRECT * CACHE_BYTES - 2;
    slot.args[6] = FS_SLOT_BYTES - (attempt == 2 ? 3 : 4);
    page = (volatile u8 *) (work.state.map.address + FS_DATA_OFFSET + lease.slot * FS_SLOT_BYTES);
    for (i = 0; i < 5; i++)
      page[FS_SLOT_BYTES - 5 + i] = 0xa5;
    CHECK(
        !__sys_copy((void *) (work.state.map.address + FS_DESC_OFFSET + lease.slot * FS_DESC_BYTES),
                    &slot, sizeof(slot)) &&
        !ipc_call(work.state.call, &call, CALL_ASYNC, &result));
    rpc = result.request;
    if (!attempt) {
      control.words[1] = 4;
      CHECK(!ipc_call(work.state.call, &control, CALL_CONTROL, &result));
      control.words[1] = 5;
      CHECK(!ipc_call(work.state.call, &control, CALL_CONTROL, &result));
    }
    if (attempt == 1) {
      control.words[1] = 1;
      CHECK(!ipc_call(work.state.call, &control, CALL_CONTROL, &result) && !ipc_rqcancel(rpc));
      control.words[1] = 2;
      CHECK(!ipc_call(work.state.call, &control, CALL_CONTROL, &result));
    }
    if (attempt == 4) {
      control.words[1] = 8;
      CHECK(!ipc_call(work.state.call, &control, CALL_CONTROL, &result));
    }
    CHECK(!ipc_rqwait(rpc, 0, &result) && result.words[1] == lease.seq &&
          page[FS_SLOT_BYTES - 5] == 0xa5);
    if (!attempt)
      CHECK(!result.words[0] && result.words[2] == 4 && result.words[3] == 4 &&
            page[FS_SLOT_BYTES - 4] == 'E' && page[FS_SLOT_BYTES - 3] == 'L' &&
            page[FS_SLOT_BYTES - 2] == 'F' && page[FS_SLOT_BYTES - 1] == '!');
    else if (attempt == 4) {
      CHECK(!result.words[0] && result.words[2] == 4 && result.words[3] == 4);
      for (i = 1; i < 5; i++)
        CHECK(!page[FS_SLOT_BYTES - 5 + i]);
    } else {
      CHECK((isize) result.words[0] == (attempt == 1   ? -EINTR
                                        : attempt == 2 ? -EINVAL
                                                       : -EIO) &&
            !result.words[2] && !result.words[3]);
      for (i = 1; i < 5; i++)
        CHECK(page[FS_SLOT_BYTES - 5 + i] == 0xa5);
    }
    CHECK(!ipc_rqwait(rpc, RQ_FINISH, &result) && !ipc_rqack(rpc, ACK_RESULT | ACK_RELEASE) &&
          !sysconnworkrelease(&work, &cache, lease));
    filecreatetrace[13]++;
  }
  control.words[1] = 3;
  control.words[2] = slot.object;
  CHECK(!ipc_call(work.state.call, &control, CALL_CONTROL, &result) && !result.words[0]);
}

static void
ttygetattr(u64 file)
{
  const Termarg expected = {0, TTY_OPOST | TTY_ONLCR, TTY_CS8, TTY_ICANON, 0, 0, {0}};
  const u8     *bytes = (const u8 *) &expected;
  Ipcmsg        result;
  Filelease     lease;
  Fileref       ref;
  Fileslot      slot = {0};
  Msg           call = {{FS_TTYCTL, work.state.map.connid, 0, 0}};
  volatile u8  *page;
  u32           attempt, i;
  CHECK(!sysfilerefview(&pool, work.state.self, file, &ref));
  slot.state = FS_SLOT_SUBMITTED;
  slot.span = 1;
  slot.object = ref.session;
  slot.payload_bytes = FS_SLOT_BYTES;
  slot.args[0] = TTY_GETATTR;
  slot.args[4] = sizeof(expected);
  for (attempt = 0; attempt < 2; attempt++) {
    CHECK(!sysconnworkreserve(&work, &cache, SYSFILE_MAIN, 1, &lease));
    slot.seq = call.words[2] = lease.seq;
    call.words[3] = lease.slot;
    slot.args[3] = FS_SLOT_BYTES - sizeof(expected) + (attempt == 0);
    page = (volatile u8 *) (uintptr_t) (work.state.map.address + FS_DATA_OFFSET +
                                        lease.slot * FS_SLOT_BYTES);
    for (i = FS_SLOT_BYTES - sizeof(expected) - 1; i < FS_SLOT_BYTES; i++)
      page[i] = 0xa5;
    CHECK(!__sys_copy((void *) (uintptr_t) (work.state.map.address + FS_DESC_OFFSET +
                                            lease.slot * FS_DESC_BYTES),
                      &slot, sizeof(slot)) &&
          !ipc_call(work.state.call, &call, 0, &result) && result.words[1] == lease.seq &&
          !result.words[2] && page[FS_SLOT_BYTES - sizeof(expected) - 1] == 0xa5);
    CHECK((isize) result.words[0] == (attempt ? 0 : -EINVAL) &&
          result.words[3] == (attempt ? sizeof(expected) : 0));
    for (i = 0; i < sizeof(expected); i++)
      CHECK(page[FS_SLOT_BYTES - sizeof(expected) + i] == (attempt ? bytes[i] : 0xa5));
    CHECK(!sysconnworkrelease(&work, &cache, lease));
  }
}

static void
client(const Bootview *boot)
{
  Opkey              key, background;
  Opencreate         late = {0};
  Rkey               latectx = {0};
  const u64          lateargs[8] = {0, 0, 0, 6};
  Fdtag              fd;
  Fdview             detached, replaced;
  Fileref            reference;
  u64                files[2], source = 0, first, second, ids[2], mask, ignored, displaced;
  Msg                gate;
  Ipcmsg             result;
  Handle             rpc, original;
  isize              closeprogress;
  Msg                hello = {{FS_HELLO, 0, 1, UINT64_MAX}};
  Msg                stale = {{FS_READ, 0x10000, 1, 0}};
  volatile Filehead *head;
  volatile Fileslot *slot;
  CHECK(!ipc_call(bootresource(boot, 256), &hello, 0, &result) &&
        (isize) result.words[0] == -EACCES && result.words[1] == 1);
  setupwithhello(boot, 2);
  hello.words[2] = 3;
  CHECK(!ipc_call(work.state.call, &hello, CALL_RECV_MEM, &result) &&
        (isize) result.words[0] == -EBUSY && result.words[1] == 3);
  CHECK(work.state.map.connid == 0x20000 && !ipc_call(work.state.call, &stale, 0, &result) &&
        (isize) result.words[0] == -ESTALE && result.words[1] == 1);
  head = (volatile Filehead *) (uintptr_t) work.state.map.address;
  slot = (volatile Fileslot *) ((volatile u8 *) head + FS_DESC_OFFSET);
  slot->seq = stale.words[2];
  slot->state = FS_SLOT_SUBMITTED;
  slot->span = 1;
  slot->object = 1;
  head->connid = stale.words[1];
  stale.words[1] = work.state.map.connid;
  CHECK(!ipc_call(work.state.call, &stale, 0, &result) && (isize) result.words[0] == -ESTALE &&
        result.words[1] == 1);
  head->connid = work.state.map.connid;
  creatorsignals();
  key = begin(FS_OPEN, "/a", 0);
  collect(&key);
  zero(files, sizeof(files));
  complete(&key, 0, files, &source);
  first = files[0];
  execmanifest(boot, first, 1);
  execmanifest(boot, first, BOOT_FD_COUNT);
  loadreads();
  ttygetattr(first);
  ttyiocall(first, FS_READ, 1);
  ttyiocall(first, FS_READ, 0);
  ttyiocall(first, FS_WRITE, 1);
  ttyiocall(first, FS_WRITE, 0);
  ttyiocall(first, FS_READ, 2);
  ttyiocall(first, FS_READ, 3);
  ttyiocall(first, FS_READ, 5);
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
  CHECK(!sysfileopenprepare(&pool, &work, &cache, &fds, 0, 100 + filecreatetrace[1], lateargs,
                            "/late", 6, 0, &late, &gate) &&
        !sysctxopenio(&contexts, work.state.self, 0, &pool, &work, &cache, &late.operation,
                      &latectx));
  filecreatetrace[1]++;
  key = late.operation;
  ids[0] = work.state.creators[key.lease.slot].files[0];
  CHECK(!sysoprpc(&work, &cache, &key, &original));
  /* A distinct request tries to reuse the exact deferred OPEN's slot. */
  gate.words[0] = FS_OPEN;
  gate.words[1] = work.state.map.connid;
  gate.words[2] = key.lease.seq;
  gate.words[3] = key.lease.slot;
  CHECK(!ipc_call(work.state.call, &gate, CALL_ASYNC, &result));
  rpc = result.request;
  CHECK(!ipc_rqwait(rpc, RQ_FINISH, &result) && !ipc_rqwait(rpc, 0, &result) &&
        (isize) result.words[0] == -EBUSY && result.words[1] == key.lease.seq && !result.words[2] &&
        !result.words[3] && !ipc_rqack(rpc, ACK_RESULT | ACK_RELEASE));
  CHECK(!sysfileopeninterrupt(&pool, &work, &cache, &fds, &late, 1) &&
        !sysfileopeninterrupt(&pool, &work, &cache, &fds, &late, 2) &&
        !sysoprpc(&work, &cache, &key, &rpc) && rpc == original &&
        work.state.operations[key.lease.slot].deadline == 1 + SYSOP_CONTROL_NS &&
        !sysfdobserve(&fds, late.tag.index, &detached) && detached.tag.state == SYSFD_RESERVED &&
        !sysopabandon(&work, &cache, &key, &background));
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
  CHECK(!files[0] && !files[1] && !source && !sysfdcancel(&fds, late.tag));
  closefile(ids[0], 0);
  filecreatetrace[3] = 1;
  {
    Opencreate missing = {0};
    Rkey       active = {0};
    char       longpath[FS_SLOT_BYTES];
    int        number = -2;
    isize      business = 9;
    u64        i, sequence = contexts.sequence;
    for (i = 0; i < sizeof(longpath); i++)
      longpath[i] = 'm';
    longpath[0] = '/';
    CHECK(sysctxopencreate(&contexts, 0, &pool, &work, &cache, &fds,
                           (const char *) (uintptr_t) BOOT_USER_HI, 0, 0, 0, &missing, &active,
                           &number, &business) == -ERR_FAULT &&
          sysctxopencreate(&contexts, 0, &pool, &work, &cache, &fds, longpath, 0, 0, 0, &missing,
                           &active, &number, &business) == -ERR_LIMIT &&
          contexts.sequence == sequence && !missing.operation.lease.seq && !active.sequence &&
          number == -2 && business == 9 && !sysfdobserve(&fds, 2, &detached) &&
          detached.tag.state == SYSFD_FREE);
    longpath[sizeof(longpath) - 1] = 0;
    CHECK(!sysctxopencreate(&contexts, 0, &pool, &work, &cache, &fds, longpath, 0, 0, 0, &missing,
                            &active, &number, &business) &&
          number == -1 && business == -ENOENT && !missing.operation.lease.seq &&
          !sysfdobserve(&fds, 2, &detached) && detached.tag.state == SYSFD_FREE);
    filecreatetrace[1]++;
  }
  CHECK(!__sys_sigmask(SIGNAL_BLOCK, SIGNAL_SUPPORTED, &mask) &&
        !sysfdobserve(&fds, 1, &detached) && detached.file == second &&
        sysfddup2(&fds, &pool, work.state.self, 0, 1, &displaced) == 1 && displaced == second &&
        !sysfilerefview(&pool, work.state.self, second, &reference) &&
        reference.state == SYSFILE_REF_CLOSING && !reference.aliases &&
        !sysfdobserve(&fds, 1, &replaced) && replaced.file == first &&
        replaced.tag.generation != detached.tag.generation &&
        !__sys_sigmask(SIGNAL_SETMASK, mask, &ignored));
  CHECK(!__sys_sigmask(SIGNAL_BLOCK, SIGNAL_SUPPORTED, &mask) &&
        sysfileclosepump(&pool, &work, &cache, 900 + filecreatetrace[2], 0) ==
            SYSFILE_CLOSE_SUBMITTED &&
        !__sys_sigmask(SIGNAL_SETMASK, mask, &ignored));
  do {
    CHECK(!__sys_sigmask(SIGNAL_BLOCK, SIGNAL_SUPPORTED, &mask));
    closeprogress = sysfileclosepump(&pool, &work, &cache, 900 + filecreatetrace[2], 0);
    CHECK(!__sys_sigmask(SIGNAL_SETMASK, mask, &ignored));
    if (closeprogress == -ERR_WOULD_BLOCK)
      CHECK(!ipc_recv(EVENT_RQ_DONE, RECV_EVENTS_ONLY, &result));
  } while (closeprogress == -ERR_WOULD_BLOCK);
  CHECK(!closeprogress);
  filecreatetrace[2]++;
  CHECK(sysfilerefview(&pool, work.state.self, second, &reference) == -ERR_STALE &&
        !sysfilerefview(&pool, work.state.self, first, &reference) && reference.aliases == 2 &&
        !sysfdobserve(&fds, 1, &detached) && detached.file == first &&
        detached.tag.generation == replaced.tag.generation);
  CHECK(!sysfdobserve(&fds, 0, &detached) && detached.file == first &&
        !sysfddetach(&fds, detached.tag, &detached) &&
        !sysfilerefdrop(&pool, work.state.self, detached.file));
  CHECK(!sysfdobserve(&fds, 1, &detached) && detached.file == first &&
        !sysfddetach(&fds, detached.tag, &detached));
  closefile(first, 1);
  resourceopens(boot);
  gate.words[0] = STOP;
  CHECK(!ipc_call(work.state.call, &gate, CALL_ASYNC, &result));
  rpc = result.request;
  CHECK(!ipc_rqwait(rpc, 0, &result) && result.words[0] == 7 && result.words[1] == 7 &&
        !result.words[2] && !ipc_rqack(rpc, ACK_RESULT | ACK_RELEASE) &&
        !sysconninvalidate(&cache, work.state.tag) && !sysfileconnclean(&pool, &work, &cache));
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
typedef struct Exitdrain Exitdrain;
struct Exitdrain
{
  Handle request;
  u64   *references;
  u32    attempts;
};

static isize
drainexit(void *context, Filelink *link)
{
  Exitdrain *drain = context;
  CHECK(link->watch.owner.reason == OBS_EXIT && drain->request && drain->references[0] &&
        drain->references[1]);
  if (++drain->attempts == 1)
    return -ERR_WOULD_BLOCK;
  CHECK(!sysfilewatchdrain(&link->watch, drain->request));
  drain->references[0] = drain->references[1] = 0;
  return 0;
}

static isize
execreference(void *context, Handle pid, u64 image, u64 session, u64 reference)
{
  u64 *key = context;
  if ((u64) pid != key[0] || image != key[1] || session != key[2] || reference != key[3])
    return -ERR_DENIED;
  key[4]++;
  return 0;
}

static isize
exectake(void *context, const Fileowner *source, const Fileowner *target, const Execgroup *group,
         u64 *hold)
{
  u64 *held = context;
  if (*held || !group->retained || source->pid != target->pid || target->image <= source->image)
    return -ERR_STATE;
  *hold = *held = 1;
  return 0;
}

static isize
execdrop(void *context, u64 hold)
{
  u64 *held = context;
  if (!hold || *held != hold)
    return -ERR_STALE;
  *held = 0;
  return 0;
}

static isize
execdrain(void *context, Filelink *link)
{
  Filelink *target = context;
  CHECK(link == target && link->watch.owner.reason == OBS_ABORT);
  return execreserve.count ? -ERR_BUSY : 0;
}

static isize
claimelfread(void *context, u64 offset, void *data, u64 bytes)
{
  (void) context;
  return __sys_copy(data, execclaim_elf + offset, bytes);
}

static void
claimsource(const Bootview *boot)
{
  typedef struct Claimboot Claimboot;
  struct Claimboot
  {
    Boot     header;
    Resentry resources[3];
  };
  Claimboot packet = {0};
  Execwork  next = {0};
  Elfsource source = {0, (u64) (execclaim_elf_end - execclaim_elf), claimelfread};
  Elfplan   plan;
  Ipcmsg    result;
  Msg       call;
  Msg       seen = {{SEEN, 0, 0, 0}};
  Fdtag     fd;
  Handle    rpc;
  Filelease lease;
  Fileslot  slot = {0};
  u64       file, pin;
  setup(boot);
  CHECK(!sysfilerefnew(&pool, work.state.self, work.state.tag, 100, 1000, 0, &file) &&
        !sysfdreserve(&fds, 0, &fd) && !sysfdpublish(&fds, fd, file, 0) &&
        !sysfilerefpin(&pool, work.state.self, file));
  CHECK(!sysfilerefnew(&pool, work.state.self, work.state.tag, 100, 1001, 0, &pin) &&
        !sysfdreserve(&fds, 1, &fd) && !sysfdpublish(&fds, fd, pin, BOOT_FD_CLOEXEC) &&
        !sysexecfds(&execfds, &fds, &pool, &cache, work.state.self));
  next.elf.parent_space = bootresource(boot, BOOT_SELF_SPACE);
  next.elf.scratch = 0x2000000;
  next.elf.scratch_bytes = BOOT_MAX_BYTES;
  CHECK(!elfcheck(&source, 128, &plan) && !sysexecprepare(&next, 0));
  pin = file;
  CHECK(!sysexecfdbegin(&execfds, &work, &cache, SYSFILE_MAIN, 1, next.elf.image, 100, &pin,
                        &execsend) &&
        !pin && !sysoprequest(&work, &cache, &execsend.key, &call) &&
        !ipc_call(work.state.call, &call, CALL_ASYNC, &result));
  rpc = result.request;
  CHECK(!sysopsubmitted(&work, &cache, &execsend.key, &result.request) &&
        !ipc_rqwait(rpc, 0, &result) && !result.words[0] && result.words[1] == call.words[2] &&
        result.words[2] == EXEC_CLAIM_TOKEN && !result.words[3] &&
        !sysopcapture(&work, &cache, &execsend.key, 0, &result) &&
        !sysopfinished(&work, &cache, &execsend.key) && !sysopsaved(&work, &cache, &execsend.key) &&
        !sysopack(&work, &cache, &execsend.key) &&
        !sysopfinish(&work, &cache, &execsend.key, &pin) && pin == file &&
        !sysfilerefunpin(&pool, work.state.self, pin) && !sysexecfdrelease(&execfds));
  packet.header.magic = BOOT_MAGIC;
  packet.header.format = BOOT_FORMAT;
  packet.header.bytes = sizeof(packet);
  packet.header.resource_count = ARRAY_SIZE(packet.resources);
  packet.header.resources_off = offsetof(Claimboot, resources);
  packet.header.user_lo = BOOT_USER_LO;
  packet.header.user_hi = BOOT_USER_HI;
  packet.resources[0].tag = BOOT_SELF_SPACE;
  packet.resources[1].tag = BOOT_SELF_ALLOC;
  packet.resources[2].tag = EXEC_CLAIM_CALL;
  packet.resources[2].handle = work.state.call;
  CHECK(!sysexecfill(&next, &source, &plan, &packet, sizeof(packet)));
  CHECK(!sysconnworkreserve(&work, &cache, SYSFILE_MAIN, 1, &lease));
  call.words[0] = FS_READ;
  call.words[2] = slot.seq = lease.seq;
  call.words[3] = lease.slot;
  slot.state = FS_SLOT_SUBMITTED;
  slot.span = 1;
  slot.object = 100;
  slot.payload_bytes = slot.args[1] = 1;
  CHECK(!__sys_copy((void *) (work.state.map.address + FS_DESC_OFFSET + lease.slot * FS_DESC_BYTES),
                    &slot, sizeof(slot)) &&
        !ipc_call(work.state.call, &call, CALL_ASYNC, &result) && result.request &&
        !ipc_call(work.state.call, &seen, CALL_CONTROL, &result) && !result.words[0]);
  (void) sysexecpublish(&next);
  CHECK(0);
}

static isize
claimlookup(void *context, Handle pid, u64 image, u64 session, u64 reference)
{
  u64 *key = context;
  if (reference == key[3] + 1)
    reference--;
  return execreference(context, pid, image, session, reference);
}

typedef struct Claimrefs Claimrefs;
struct Claimrefs
{
  Openprep   refs[3];
  Filewatch *old;
  Handle     pending;
  u32        steps;
};

static isize
claimactivate(void *context, const Fileowner *target, u64 hold)
{
  Claimrefs *refs = context;
  CHECK(!refs->steps && target->image == 2 && !target->reason && hold == 1);
  refs->steps = 1;
  return 0;
}

static isize
claimclose(void *context, const Openprep *prep)
{
  Claimrefs *refs = context;
  CHECK(prep->state == Openattached && prep->client_image == 1 && prep->session == 100);
  if (prep->reference == 1000) {
    CHECK(refs->steps == 1);
    refs->steps = 2;
    return 0;
  }
  CHECK(prep->reference == 1001 && (refs->steps == 2 || refs->steps == 3));
  if (++refs->steps == 3)
    return -ERR_WOULD_BLOCK;
  return sysfilewatchdrain(refs->old, refs->pending);
}

static isize
claimretire(void *context, const Fileowner *source, const Execgroup *group)
{
  Claimrefs *refs = context;
  CHECK(source->image == 1 && source->reason == OBS_EXEC);
  CHECK(group->reference == 1000 ? group->retained == 1 && !group->closed
                                 : !group->retained && group->closed == 1);
  return sysopenclose(refs->refs, ARRAY_SIZE(refs->refs), source->pid, source->image,
                      group->session, group->reference, claimclose, refs);
}

static void
claimbackend(const Bootview *boot)
{
  Filelink   links[2] = {0};
  Filewatch *old = &links[0].watch;
  Fileowner *target = &links[1].watch.owner;
  Handle     parent, permit = bootresource(boot, 258), space = bootresource(boot, BOOT_SELF_SPACE);
  Handle     alloc = bootresource(boot, BOOT_SELF_ALLOC);
  Connpeer   self;
  Ipcmsg     input;
  Fileslot   slot;
  u64        key[5], held = 0;
  Claimrefs  refs = {0};
  u32        i, pending_slot;
  volatile u64 *marker;
  Msg           stale = {{(u64) -ESTALE, 2, 0, 0}};
  Msg           seen = {{0, 0, 0, 0}};
  CHECK(!__sys_getid(&self.pid, &self.image, &parent) && !ipc_recv(0, 0, &input) &&
        !sysfilehello(links, ARRAY_SIZE(links), permit, alloc, space, ADDRESS, &input) &&
        !ipc_recv(0, 0, &input) && !sysfilelinkaccept(links, ARRAY_SIZE(links), &input, &slot));
  key[0] = (u64) old->owner.pid;
  key[1] = old->owner.image;
  key[2] = 100;
  key[3] = 1000;
  key[4] = 0;
  for (i = 0; i < ARRAY_SIZE(refs.refs); i++) {
    refs.refs[i].client = old->owner.pid;
    refs.refs[i].client_image = i == 2 ? 2 : 1;
    refs.refs[i].session = key[2];
    refs.refs[i].reference = key[3] + (i == 1);
    sysopencommit(&refs.refs[i]);
  }
  CHECK(!sysexecmanifest(&execplan, old, &input, &slot,
                         (const void *) (ADDRESS + FS_DATA_OFFSET + input.words[3] * FS_SLOT_BYTES),
                         self, 0, claimlookup, key) &&
        key[4] == 2 && !sysexecobserve(target, permit, &input, slot.args[1]) &&
        !sysexecreserve(&execreserve, &execplan, &old->owner, target, exectake, execdrop, &held) &&
        !sysfileprepay(links, ARRAY_SIZE(links), 1, alloc, space, ADDRESS) &&
        links[1].watch.connid == EXEC_CLAIM_CONNID);
  marker = (volatile u64 *) (links[1].address + FS_DATA_OFFSET);
  *marker = EXEC_CLAIM_MARKER;
  refs.old = old;
  CHECK(sysexecadvance(&execreserve, &old->owner, target, claimactivate, claimretire, &refs) ==
            -ERR_BUSY &&
        !refs.steps && !sysexecreply(&execreserve, old, &links[1], &input, EXEC_CLAIM_TOKEN, 0));
  CHECK(!ipc_recv(0, 0, &input) && input.words[0] == FS_READ &&
        !sysfilelinkaccept(links, ARRAY_SIZE(links), &input, &slot) &&
        !sysfileargs(FS_READ, &slot));
  pending_slot = (u32) input.words[3];
  refs.pending = input.request;
  CHECK(!ipc_defer(refs.pending) && sysfilewatchdrain(old, refs.pending) == -ERR_BUSY &&
        !ipc_recv(0, 0, &input) && input.words[0] == SEEN && !ipc_reply(input.request, &seen));
  CHECK(!ipc_recv(EVENT_LIFE, RECV_EVENTS_ONLY, &input) && input.kind == IPC_EVENT &&
        input.words[0] == EVENT_LIFE &&
        sysexecadvance(&execreserve, &old->owner, target, claimactivate, claimretire, &refs) ==
            -ERR_WOULD_BLOCK &&
        refs.steps == 3 && refs.refs[0].state == Openclosed && refs.refs[1].state == Openattached &&
        links[1].prepaid && !links[1].ready && old->active.requests[pending_slot] == refs.pending &&
        sysfilelinkclose(&links[0]) == -ERR_BUSY && links[0].memory && links[0].address &&
        !sysexecadvance(&execreserve, &old->owner, target, claimactivate, claimretire, &refs) &&
        refs.steps == 4 && refs.refs[1].state == Openclosed && refs.refs[2].state == Openattached &&
        !old->active.requests[pending_slot] && sysfilewatchdrain(old, refs.pending) == -ERR_STALE &&
        !sysopenclose(refs.refs, ARRAY_SIZE(refs.refs), old->owner.pid, 1, 100, 1000, claimclose,
                      &refs) &&
        refs.steps == 4);
  CHECK(!ipc_recv(0, 0, &input) && !sysexecclaim(&execreserve, &old->owner, &links[1], &input) &&
        old->owner.reason == OBS_EXEC && links[1].ready && !links[1].prepaid && held &&
        !ipc_recv(0, 0, &input) && *marker == ~EXEC_CLAIM_MARKER &&
        sysexecclaim(&execreserve, &old->owner, &links[1], &input) == -ERR_STALE &&
        !ipc_reply(input.request, &stale));
  do {
    CHECK(!ipc_recv(EVENT_LIFE, 0, &input) && input.kind == IPC_EVENT &&
          input.words[0] == EVENT_LIFE && !sysfileownerread(target));
  } while (!target->reason);
  CHECK(target->reason == OBS_EXIT &&
        !sysexecdiscard(&execreserve, &old->owner, target, execdrop, &held) && !held &&
        !sysfilelinkclose(&links[1]) && !sysfilelinkclose(&links[0]));
  filecreatetrace[0] = 2;
}

static void
server(const Bootview *boot, u32 recover)
{
  Connpeer   peer;
  Resreply   transfer;
  Ipcmsg     input, deferred;
  Msg        reply;
  Handle     space = bootresource(boot, BOOT_SELF_SPACE), pending = 0;
  Filehead  *head = (Filehead *) ADDRESS;
  Fileslot   descriptor;
  Filelink   links[2] = {0};
  Filelink  *link = &links[0];
  Filewatch *watch = &link->watch;
  Fileowner *target = &links[1].watch.owner;
  u64        held = 0;
  u64        sessions[8], references[8], created = 0, closed = 0, nextsession = 100, nextref = 1000;
  u64        slot, seq, opcode, i, found, number, resourcepath, firsttoken = 0;
  volatile u64   *data;
  isize           status;
  const Termarg   ttymode = {0, TTY_OPOST | TTY_ONLCR, TTY_CS8, TTY_ICANON, 0, 0, {0}};
  Ttyserial       ttyio;
  Ttyserialresult ttyresult;
  Ttypending      ttydeferred = {0};
  Ttyalarm        readalarm = {0};
  Loadio          loadio = {0};
  Cache           loadcache = {0};
  Loadspan        loadspan;
  Loadcache       loadwork = {0};
  Loadfetch       fetch;
  Cachekey        competing = {0};
  Cacheio         duplicate;
  u8              loadtarget[CACHE_BYTES], *unused = NULL;
  u8              loadbytes[4];
  u64             loadsession = 0, guard;
  u64             ttynow, ttyalarm = 0, generation;
  u8              output[3];
  u32             ttyseen = 0;
  if (!recover)
    CHECK(!ttyinputinit(&ttyline, 1) && !serialinit(&ttyrx, 1) && !serialinit(&ttytx, 1) &&
          !ttyserialinit(&ttyio, &ttyrx, &ttytx, 1));
  CHECK(!sysconnpeer(bootresource(boot, 256), &peer) && !ipc_recv(0, 0, &input) &&
        input.kind == IPC_CALL && input.source == peer.pid && input.image == peer.image &&
        input.words[0] == FS_HELLO && !input.words[1] && input.words[2] &&
        input.words[3] == UINT64_MAX);
  if (!recover) {
    CHECK(sysfilehello(links, ARRAY_SIZE(links), bootresource(boot, 258),
                       bootresource(boot, BOOT_SELF_ALLOC), space, ADDRESS,
                       &input) == -ERR_DENIED &&
          !link->memory && !link->address && !watch->owner.cookie && !link->ready);
    zero(&reply, sizeof(reply));
    reply.words[0] = (u64) -EACCES;
    reply.words[1] = input.words[2];
    CHECK(!ipc_reply(input.request, &reply) && !ipc_recv(0, 0, &input) &&
          input.source == peer.pid && input.image == peer.image);
  }
  CHECK(!sysfilehello(links, ARRAY_SIZE(links), bootresource(boot, 258),
                      bootresource(boot, BOOT_SELF_ALLOC), space, ADDRESS, &input));
  zero(sessions, sizeof(sessions));
  zero(references, sizeof(references));
  for (;;) {
    u64 events = readalarm.deadline ? EVENT_TIMER : 0;
    if (readalarm.deadline && ttyseen == 8)
      events |= EVENT_RQ_CANCEL;
    CHECK(!ipc_recv(recover == 12 ? EVENT_LIFE : events, 0, &input));
    if (input.kind == IPC_EVENT && (input.words[0] & EVENT_TIMER)) {
      Termarg raw = {0, 0, TTY_CS8, 0, 0, 0, {0}};
      raw.cc[TTY_VTIME] = 1;
      CHECK(ttydeferred.call.request && ttyseen == 6 && readalarm.deadline &&
            input.words[1] == readalarm.generation && !sysfilenow(&ttynow) &&
            ttynow >= readalarm.deadline && serialwrite(&ttyrx, 1, "z", 1) == 1 &&
            !ttyfileadvance(&ttydeferred, &ttyio, &ttyline, &raw, &watch->active, ttynow, &ttyalarm,
                            &ttyresult) &&
            !ttyalarmset(&readalarm, ttyalarm) && !ttyalarm && !ttydeferred.call.request &&
            ttyresult.again && !ttyresult.hints && !ttyline.ready &&
            serialread(&ttyrx, 1, output, 1) == 1 && output[0] == 'z');
      continue;
    }
    if (input.kind == IPC_EVENT && (input.words[0] & EVENT_RQ_CANCEL)) {
      Termarg raw = {0, 0, TTY_CS8, 0, 0, 0, {0}};
      raw.cc[TTY_VTIME] = 50;
      generation = readalarm.generation;
      CHECK(ttydeferred.call.request && !sysfilenow(&ttynow) && ttynow < readalarm.deadline &&
            !ttyfileadvance(&ttydeferred, &ttyio, &ttyline, &raw, &watch->active, ttynow, &ttyalarm,
                            &ttyresult) &&
            !ttyalarmset(&readalarm, ttyalarm));
      if (!ttydeferred.call.request)
        CHECK(ttyseen == 8 && !readalarm.deadline && readalarm.generation > generation);
      continue;
    }
    if (input.kind == IPC_EVENT) {
      Exitdrain drain = {pending, references, 0};
      Handle    pid;
      u64       state, reason, image, cookie = watch->owner.cookie;
      CHECK(recover == 12 && input.words[0] == EVENT_LIFE && pending && created == 2 && !closed &&
            sysfilelinksweep(links, ARRAY_SIZE(links), drainexit, &drain) == -ERR_WOULD_BLOCK &&
            watch->owner.cookie == cookie && link->memory && link->address);
      CHECK(sysfilelinkaccept(links, ARRAY_SIZE(links), &deferred, &descriptor) == -ERR_PEER &&
            sysfilelinkclose(link) == -ERR_BUSY);
      CHECK(!sysfilelinksweep(links, ARRAY_SIZE(links), drainexit, &drain) && drain.attempts == 2 &&
            !references[0] && !references[1] && !link->memory && !link->address &&
            !watch->owner.cookie &&
            __sys_obsread(cookie, &state, &reason, &pid, &image) == -ERR_STALE);
      CHECK(!sysfilelinksweep(links, ARRAY_SIZE(links), drainexit, &drain) && drain.attempts == 2);
      closed = 2;
      break;
    }
    CHECK(input.kind == IPC_CALL && input.source == peer.pid && input.image == peer.image);
    opcode = input.words[0];
    zero(&reply, sizeof(reply));
    if (opcode == FS_HELLO) {
      CHECK(sysfilehello(links, ARRAY_SIZE(links), bootresource(boot, 258),
                         bootresource(boot, BOOT_SELF_ALLOC), space, ADDRESS,
                         &input) == -ERR_BUSY &&
            !links[1].generation);
      reply.words[0] = (u64) -EBUSY;
      reply.words[1] = input.words[2];
      CHECK(!ipc_reply(input.request, &reply));
      continue;
    }
    if (opcode == STOP) {
      CHECK(!pending && created == (recover ? 0 : 7) && closed == (recover ? 0 : 7));
      CHECK(!loadsession && !loadio.request);
      for (i = 0; i < FS_CONN_SLOTS; i++)
        CHECK(!watch->active.requests[i]);
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
      CHECK(!ipc_reply(pending, &reply) && !sysfilefinish(&watch->active, pending));
      pending = 0;
      continue;
    }
    if (opcode == SEEN) {
      if (!recover) {
        if (ttyseen == 8) {
          Termarg raw = {0, 0, TTY_CS8, 0, 0, 0, {0}};
          Termarg updated = {0, TTY_OPOST, TTY_CS8, 0, 0, 0, {0}};
          Handle  original = ttydeferred.call.request;
          raw.cc[TTY_VTIME] = 50;
          updated.cc[TTY_VTIME] = 50;
          generation = readalarm.generation;
          CHECK(ttydeferred.call.request && !sysfilenow(&ttynow) && ttynow < readalarm.deadline &&
                ttyfilemode(&ttyline, &raw, &updated, &ttydeferred, readalarm.deadline) ==
                    -ERR_BUSY &&
                !raw.oflag && !ttyfilemode(&ttyline, &raw, &updated, &ttydeferred, ttynow) &&
                raw.oflag == TTY_OPOST &&
                !ttyfileadvance(&ttydeferred, &ttyio, &ttyline, &raw, &watch->active, ttynow,
                                &ttyalarm, &ttyresult) &&
                ttydeferred.call.request && ttyalarm == readalarm.deadline &&
                !ttyalarmset(&readalarm, ttyalarm) && readalarm.generation == generation);
          updated.cc[TTY_VTIME] = 49;
          CHECK(!ttyfilemode(&ttyline, &raw, &updated, &ttydeferred, ttynow) &&
                ttydeferred.call.request == original &&
                !ttyfileadvance(&ttydeferred, &ttyio, &ttyline, &raw, &watch->active, ttynow,
                                &ttyalarm, &ttyresult) &&
                ttydeferred.call.request == original && ttyalarm == ttynow + UINT64_C(4900000000) &&
                !ttyalarmset(&readalarm, ttyalarm) && readalarm.generation > generation &&
                !ipc_reply(input.request, &reply));
          continue;
        }
        if (ttyseen == 4 && !ttydeferred.call.request) {
          CHECK(!ttyinputinit(&ttyline, 0) && !ipc_reply(input.request, &reply));
          ttyseen++;
          continue;
        }
        CHECK(ttydeferred.call.request && ttyseen < 4);
        if (++ttyseen == 2) {
          CHECK(serialwrite(&ttyrx, 1, "b\n", 2) == 2 &&
                !ttyfileadvance(&ttydeferred, &ttyio, &ttyline, &ttymode, &watch->active, 0,
                                &ttyalarm, &ttyresult) &&
                !ttydeferred.call.request && !ttyline.ready && ttyresult.again &&
                !ttyresult.hints &&
                !ttyfileadvance(NULL, &ttyio, &ttyline, &ttymode, NULL, 0, &ttyalarm, &ttyresult) &&
                ttyline.ready == 2 && ttyresult.hints == SERIAL_RX_ROOM);
        }
        if (ttyseen == 4) {
          CHECK(!ttyfileadvance(&ttydeferred, &ttyio, &ttyline, &ttymode, &watch->active, 0,
                                &ttyalarm, &ttyresult) &&
                !ttydeferred.call.request && ttyio.queued == 1 && ttyresult.again &&
                !ttyresult.hints);
        }
        CHECK(!ipc_reply(input.request, &reply));
        continue;
      }
      CHECK(recover == 12 && pending && created == 2 && !ipc_reply(input.request, &reply));
      continue;
    }
    if (opcode == LOADTEST) {
      CHECK(!recover);
      if (!input.words[1]) {
        CHECK(!cacheinit(&loadcache, loadblocks, ARRAY_SIZE(loadblocks), 128));
        CHECK(!loadsession &&
              !loadopen(&loadtable, &loadfile, input.source, input.image, 0, 100, &loadsession));
        reply.words[2] = loadsession;
      } else if (input.words[1] == 1) {
        CHECK(loadio.request && loadwrite(&loadtable, loadfile.inode, &guard) == -ERR_BUSY);
      } else if (input.words[1] == 2) {
        CHECK(loadio.request && !loadioreply(&loadio, NULL, 0, 3));
      } else if (input.words[1] == 4) {
        Cacheio previous = fetch.ticket;
        CHECK(loadio.request && loadio.page[loadio.dataoff] == 0xa5);
        zero(loadtarget, CACHE_BYTES);
        loadtarget[CACHE_BYTES - 2] = 'E';
        loadtarget[CACHE_BYTES - 1] = 'L';
        CHECK(loadcachecomplete(&loadwork, input.request, &fetch.ticket, loadtarget,
                                sizeof(loadtarget)) == -ERR_STALE &&
              loadcachecomplete(&loadwork, loadio.request, &fetch.ticket, loadtarget,
                                sizeof(loadtarget) - 1) == -ERR_INVALID &&
              !loadcachecomplete(&loadwork, loadio.request, &fetch.ticket, loadtarget,
                                 sizeof(loadtarget)) &&
              cacheget(&loadcache, 110, CACHE_DATA, &competing) == -ERR_CAPACITY &&
              cachefetch(&loadcache, loadspan.first, CACHE_DATA, &duplicate, &unused) == 0 &&
              !unused && loadcachepump(&loadwork, &loadio, &fetch, loadbytes, 3) == 1 &&
              fetch.block == loadspan.next &&
              loadcachecomplete(&loadwork, loadio.request, &previous, loadtarget,
                                sizeof(loadtarget)) == -ERR_STALE &&
              loadcachepump(&loadwork, &loadio, &fetch, loadbytes, 3) == -ERR_WOULD_BLOCK);
      } else if (input.words[1] == 5) {
        CHECK(loadio.request && loadio.page[loadio.dataoff] == 0xa5);
        zero(loadtarget, CACHE_BYTES);
        loadtarget[0] = 'F';
        loadtarget[1] = '!';
        CHECK(!loadcachecomplete(&loadwork, loadio.request, &fetch.ticket, loadtarget,
                                 sizeof(loadtarget)) &&
              !loadcachepump(&loadwork, &loadio, &fetch, loadbytes, 3) &&
              !cacheget(&loadcache, 110, CACHE_DATA, &competing) &&
              !cacheput(&loadcache, &competing));
      } else if (input.words[1] == 6 || input.words[1] == 7) {
        CHECK(!loadio.request && !loadwork.request &&
              !cacheget(&loadcache, 99, CACHE_DATA, &competing));
        if (input.words[1] == 6) {
          loadblocks[competing.index].serial = UINT64_MAX;
          CHECK(!cacheput(&loadcache, &competing));
        } else
          CHECK(!cacheinvalidate(&loadcache, &competing));
      } else if (input.words[1] == 8) {
        CHECK(loadio.request && fetch.block == 99);
        zero(loadtarget, CACHE_BYTES);
        CHECK(!loadcachecomplete(&loadwork, loadio.request, &fetch.ticket, loadtarget,
                                 sizeof(loadtarget)) &&
              !loadcachepump(&loadwork, &loadio, &fetch, loadbytes, 3));
      } else {
        CHECK(input.words[1] == 3 && input.words[2] == loadsession && !loadio.request &&
              !loadclose(&loadtable, loadsession, input.source, input.image) &&
              !loadwrite(&loadtable, loadfile.inode, &guard) && !loadunwrite(&loadtable, guard));
        loadsession = 0;
      }
      CHECK(!ipc_reply(input.request, &reply));
      continue;
    }
    if (opcode == FS_EXEC_CLAIM) {
      CHECK(!recover && target->cookie && held &&
            sysexecclaim(&execreserve, &watch->owner, &links[1], &input) == -ERR_STALE &&
            execreserve.token == 41 && !links[1].ready);
      reply.words[0] = (u64) -ESTALE;
      reply.words[1] = input.words[2];
      CHECK(!ipc_reply(input.request, &reply));
      continue;
    }
    if (opcode == EXECWATCH) {
      CHECK(!recover && target->cookie && held &&
            sysfilelinksweep(links, ARRAY_SIZE(links), execdrain, &links[1]) == -ERR_BUSY &&
            links[1].prepaid && links[1].memory &&
            !sysexecdiscard(&execreserve, &watch->owner, target, execdrop, &held) && !held &&
            !execreserve.count && target->reason == OBS_ABORT && !watch->owner.reason &&
            !sysfilelinksweep(links, ARRAY_SIZE(links), execdrain, &links[1]) && !target->cookie &&
            !links[1].memory && !links[1].address && !links[1].prepaid &&
            !ipc_reply(input.request, &reply));
      continue;
    }
    seq = input.words[2];
    slot = input.words[3];
    reply.words[1] = seq;
    status = sysfilelinkaccept(links, ARRAY_SIZE(links), &input, &descriptor);
    if (status == -ERR_STALE) {
      CHECK(!recover && opcode == FS_READ && !created && !watch->active.requests[0]);
      reply.words[0] = (u64) -ESTALE;
      CHECK(!ipc_reply(input.request, &reply));
      continue;
    }
    if (status == -ERR_BUSY) {
      CHECK(pending && opcode == FS_OPEN && watch->active.requests[slot] == pending);
      reply.words[0] = (u64) -EBUSY;
      CHECK(!ipc_reply(input.request, &reply));
      continue;
    }
    CHECK(!status && (descriptor.span == 1 || opcode == FS_EXEC_PREPARE));
    if (opcode == FS_READ || opcode == FS_WRITE || opcode == FS_CLOSE || opcode == FS_DUPREF)
      CHECK(!sysfileargs(opcode, &descriptor));
    data = (volatile u64 *) (ADDRESS + FS_DATA_OFFSET + slot * FS_SLOT_BYTES);
    if (!recover && opcode == FS_EXEC_PREPARE) {
      Connpeer backend;
      Handle   parent;
      u64      count = descriptor.args[4];
      u64      key[5] = {(u64) peer.pid, peer.image, sessions[0], references[0], 0};
      CHECK(!__sys_getid(&backend.pid, &backend.image, &parent) &&
            (count == 1 || count == BOOT_FD_COUNT));
      backend.image++;
      CHECK(sysexecmanifest(&execplan, watch, &input, &descriptor, (const void *) data, backend, 0,
                            execreference, key) == -ERR_STALE &&
            !execplan.count && !key[4]);
      backend.image--;
      CHECK(sysexecmanifest(&execplan, watch, &input, &descriptor, (const void *) data, backend,
                            100, execreference, key) == -ERR_TIMEDOUT &&
            !execplan.count && !key[4]);
      CHECK(!sysexecmanifest(&execplan, watch, &input, &descriptor, (const void *) data, backend, 0,
                             execreference, key) &&
            execplan.count == 1 && key[4] == 1 && execplan.groups[0].session == sessions[0] &&
            execplan.groups[0].reference == references[0] &&
            execplan.groups[0].retained == count / 2 &&
            execplan.groups[0].closed == (count + 1) / 2);
      if (count == 1)
        CHECK(sysexecobserve(target, bootresource(boot, 258), &input, descriptor.args[1]) ==
                  -ERR_STALE &&
              !target->cookie);
      else
        CHECK(!sysexecobserve(target, bootresource(boot, 258), &input, descriptor.args[1]) &&
              target->cookie && target->pid == input.source &&
              target->image == descriptor.args[1] &&
              !sysexecreserve(&execreserve, &execplan, &watch->owner, target, exectake, execdrop,
                              &held) &&
              held == 1 && execreserve.count == 1 &&
              sysexecdiscard(&execreserve, &watch->owner, target, execdrop, &held) == -ERR_BUSY);
      for (i = 0; i < count; i++)
        CHECK(execplan.rows[i].fd == i && execplan.rows[i].flags == (i & 1 ? 0U : BOOT_FD_CLOEXEC));
      execplan.count = 0;
      if (count == 1) {
        reply.words[0] = (u64) -ENOTSUP;
        CHECK(!ipc_reply(input.request, &reply) && !sysfilefinish(&watch->active, input.request));
      } else {
        Ipcmsg wrong = input;
        wrong.words[2]++;
        CHECK(sysexecreply(&execreserve, watch, &links[1], &input, 41, 0) == -ERR_STATE &&
              !sysfileprepay(links, ARRAY_SIZE(links), 1, bootresource(boot, BOOT_SELF_ALLOC),
                             space, ADDRESS) &&
              sysfileprepay(links, ARRAY_SIZE(links), 1, bootresource(boot, BOOT_SELF_ALLOC), space,
                            ADDRESS) == -ERR_BUSY &&
              !sysfilelinksweep(links, ARRAY_SIZE(links), execdrain, &links[1]) &&
              links[1].prepaid && !links[1].ready && links[1].watch.connid == 0x10001 &&
              links[1].memory && held);
        CHECK(sysexecreply(&execreserve, watch, &links[1], &wrong, 41, 0) == -ERR_STALE &&
              !execreserve.token && watch->active.requests[slot] == input.request &&
              !sysexecreply(&execreserve, watch, &links[1], &input, 41, 0) &&
              execreserve.token == 41 && !watch->active.requests[slot] &&
              !watch->active.requests[slot + 1]);
      }
      continue;
    }
    if (!recover && opcode == FS_LOAD) {
      status = loadiostart(&loadio, &loadtable, &watch->active, &input, &descriptor,
                           (volatile u8 *) data, 2);
      if (status == -ERR_INVALID) {
        reply.words[0] = (u64) -EINVAL;
        CHECK(!ipc_reply(input.request, &reply) && !sysfilefinish(&watch->active, input.request));
      } else {
        CHECK(!status);
        if (loadio.offset == 4)
          CHECK(!ipc_defer(input.request));
        else {
          CHECK(loadio.bytes == sizeof(loadbytes) &&
                !loadmapspan(&loadio.file, &loadmap, &loadindex, 99, 128, loadio.offset,
                             loadio.bytes, &loadspan) &&
                !loadcachebegin(&loadwork, &loadio, &loadcache, &loadspan));
          status = loadcachepump(&loadwork, &loadio, &fetch, loadbytes, 2);
          if (!loadio.request) {
            CHECK(!status && !loadio.offset && !loadwork.request);
            continue;
          }
          CHECK(status == 1 && fetch.block == loadspan.first &&
                loadcachepump(&loadwork, &loadio, &fetch, loadbytes, 2) == -ERR_WOULD_BLOCK &&
                cachefetch(&loadcache, loadspan.first, CACHE_DATA, &duplicate, &unused) ==
                    -ERR_WOULD_BLOCK &&
                !unused && !ipc_defer(input.request));
        }
      }
      continue;
    }
    if (!recover && opcode == FS_TTYCTL) {
      found = 0;
      for (i = 0; i < created; i++)
        if (references[i] && sessions[i] == descriptor.object)
          found++;
      CHECK(found == 1 &&
            !ttyfilestart(&ttyio, &ttyline, &ttymode, &watch->active, &input, &descriptor,
                          (volatile u8 *) data, 0, 0, NULL, &ttyalarm) &&
            !ttyalarm);
      continue;
    }
    if (!recover && (opcode == FS_READ || opcode == FS_WRITE)) {
      found = 0;
      for (i = 0; i < created; i++)
        if (references[i] && sessions[i] == descriptor.object)
          found++;
      CHECK(found == 1);
      if (ttydeferred.call.request) {
        Handle original = ttydeferred.call.request;
        CHECK(ttyseen == (opcode == FS_READ ? 1 : 3) &&
              !ttyfilestart(&ttyio, &ttyline, &ttymode, &watch->active, &input, &descriptor,
                            (volatile u8 *) data, 0, 0, &ttydeferred, &ttyalarm) &&
              !ttyalarm && ttydeferred.call.request == original && !watch->active.requests[slot]);
        continue;
      }
      if (ttyseen == 5 || ttyseen == 7) {
        Termarg raw = {0, 0, TTY_CS8, 0, 0, 0, {0}};
        raw.cc[TTY_VTIME] = ttyseen == 7 ? 50 : 1;
        CHECK(opcode == FS_READ && !sysfilenow(&ttynow) &&
              !ttyfilestart(&ttyio, &ttyline, &raw, &watch->active, &input, &descriptor,
                            (volatile u8 *) data, 0, ttynow, &ttydeferred, &ttyalarm) &&
              ttydeferred.call.request == input.request && ttyalarm > ttynow);
        CHECK(!readalarm.deadline && !ttyalarmset(&readalarm, ttyalarm));
        ttyseen++;
        continue;
      }
      if (ttyseen == 6) {
        Termarg raw = {0, 0, TTY_CS8, 0, 0, 0, {0}};
        raw.cc[TTY_VMIN] = 2;
        raw.cc[TTY_VTIME] = 1;
        CHECK(opcode == FS_READ && !sysfilenow(&ttynow) &&
              !ttyfilestart(&ttyio, &ttyline, &raw, &watch->active, &input, &descriptor,
                            (volatile u8 *) data, 0, ttynow, &ttydeferred, &ttyalarm) &&
              ttydeferred.call.request == input.request && !ttyalarm &&
              serialwrite(&ttyrx, 1, "ab", 2) == 2 &&
              !ttyfileadvance(&ttydeferred, &ttyio, &ttyline, &raw, &watch->active, ttynow,
                              &ttyalarm, &ttyresult) &&
              ttyalarm == ttynow + UINT64_C(100000000) && ttyline.ready == 1 && ttyresult.again &&
              ttyresult.hints == SERIAL_RX_ROOM &&
              !ttyfileadvance(&ttydeferred, &ttyio, &ttyline, &raw, &watch->active, ttyalarm,
                              &ttyalarm, &ttyresult) &&
              !ttyalarm && !ttydeferred.call.request && ttyresult.again && !ttyresult.hints &&
              !ttyline.ready && serialread(&ttyrx, 1, output, 1) == 1 && output[0] == 'b');
        ttyseen++;
        continue;
      }
      CHECK(!ttyfilestart(&ttyio, &ttyline, &ttymode, &watch->active, &input, &descriptor,
                          (volatile u8 *) data, 0, 0, &ttydeferred, &ttyalarm) &&
            !ttyalarm);
      if (ttyseen == 4 && opcode == FS_WRITE) {
        CHECK(ttydeferred.call.request == input.request &&
              !ttyfileadvance(&ttydeferred, &ttyio, &ttyline, &ttymode, &watch->active, 0,
                              &ttyalarm, &ttyresult) &&
              !ttydeferred.call.request && ttyio.queued == 3 && ttyresult.again &&
              ttyresult.hints == SERIAL_TX_READY &&
              serialread(&ttytx, 1, output, sizeof(output)) == 1 && output[0] == 'a' &&
              !ttyfileadvance(NULL, &ttyio, &ttyline, &ttymode, NULL, 0, &ttyalarm, &ttyresult) &&
              !ttyio.queued && ttyresult.hints == SERIAL_TX_READY &&
              serialread(&ttytx, 1, output, sizeof(output)) == 3 && output[0] == 'b' &&
              output[1] == '\r' && output[2] == '\n');
      } else if (ttydeferred.call.request) {
        CHECK(ttyseen == (opcode == FS_READ ? 0 : 2) &&
              ttyfiledefer(&ttydeferred, &watch->active, &input, &descriptor, (volatile u8 *) data,
                           NULL) == -ERR_BUSY &&
              ttyfileresume(&ttydeferred, &ttyio, &ttyline, ttymode.oflag, &watch->active, 0,
                            &ttyalarm) == -ERR_WOULD_BLOCK &&
              ttydeferred.call.request == input.request);
      } else {
        CHECK(opcode == FS_READ && ttyseen == 2 && !ttyline.ready &&
              ttyserialoutput(&ttyio, 0, "a", 1) == 1);
      }
      continue;
    }
    if ((opcode == FS_READ || opcode == FS_WRITE) && recover >= 9 && recover <= 11) {
      CHECK(created == 1 && references[0] && sessions[0] == descriptor.object && slot < 2 &&
            descriptor.payload_bytes == 8 && !descriptor.args[0] && descriptor.args[1] == 8 &&
            !descriptor.args[2] && !descriptor.args[3]);
      filecreatetrace[8]++;
      if (recover == 11 && filecreatetrace[8] == 1) {
        CHECK(opcode == FS_READ);
        data[0] = IO_VALUE;
        reply.words[2] = reply.words[3] = 8;
        CHECK(!ipc_reply(input.request, &reply) && !sysfilefinish(&watch->active, input.request));
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
          CHECK(!ipc_reply(input.request, &reply) && !sysfilefinish(&watch->active, input.request));
          continue;
        }
        resourcepath = path[1] == 'b' || path[1] == 'c' || path[1] == 'f' || path[1] == 'r';
      }
      if (resourcepath) {
        Msg    prepare;
        Ipcmsg result;
        Handle rpc;
        u64    ordinal = ++filecreatetrace[4], token;
        if (recover == 14 && ordinal == 2) {
          prepare.words[0] = FS_ABORT;
          prepare.words[1] = 0;
          prepare.words[2] = 1;
          prepare.words[3] = firsttoken;
          CHECK(!ipc_call(bootresource(boot, 257), &prepare, CALL_CONTROL, &result) &&
                (isize) result.words[0] == -EACCES && result.words[1] == 1);
        }
        prepare.words[0] = PREPARE;
        prepare.words[1] = 200 + ordinal;
        prepare.words[2] = 2000 + ordinal;
        prepare.words[3] = ordinal;
        CHECK(!ipc_call(bootresource(boot, 257), &prepare, CALL_ASYNC | CALL_CONTROL, &result));
        rpc = result.request;
        CHECK(!ipc_rqwait(rpc, 0, &result) && !result.words[0] && result.words[1] &&
              !ipc_rqack(rpc, ACK_RESULT | ACK_RELEASE));
        token = result.words[1];
        if (ordinal == 1)
          firsttoken = token;
        data[0] = prepare.words[2];
        zero(&transfer, sizeof(transfer));
        transfer.source = bootresource(boot, 257);
        transfer.rights = RESOURCE_USE;
        transfer.business[0] = seq;
        transfer.business[1] = prepare.words[1];
        transfer.business[2] = token;
        CHECK(!ipc_reply_resource(input.request, &transfer) &&
              !sysfilefinish(&watch->active, input.request));
        if (recover == 13 && ordinal == 2) {
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
    CHECK(!ipc_reply(input.request, &reply) && !sysfilefinish(&watch->active, input.request));
  }
  CHECK(!__sys_free(bootresource(boot, 256)));
  if (recover == 12)
    filecreatetrace[3] = 1;
  else
    CHECK(!sysfilelinkclose(link));
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
  Fileref   reference;
  Filelease lease;
  Fileslot  slot;
  Handle    rpc;
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
  CHECK(!sysfilerefview(&pool, backend_work.state.self, bound, &reference) &&
        !sysconnworkreserve(&backend_work, &cache, SYSFILE_CONTROL, 1, &lease));
  zero(&slot, sizeof(slot));
  slot.seq = lease.seq;
  slot.state = FS_SLOT_SUBMITTED;
  slot.object = reference.session;
  slot.span = 1;
  slot.args[0] = reference.reference ^ (UINT64_C(1) << 63);
  CHECK(slot.args[0] &&
        !__sys_copy((void *) (uintptr_t) (backend_work.state.map.address + FS_DESC_OFFSET +
                                          lease.slot * FS_DESC_BYTES),
                    &slot, sizeof(slot)));
  query.words[0] = FS_CLOSE;
  query.words[1] = CONNID;
  query.words[2] = lease.seq;
  query.words[3] = lease.slot;
  CHECK(!ipc_call(backend_work.state.call, &query, CALL_ASYNC | CALL_CONTROL, &result));
  rpc = result.request;
  CHECK(!ipc_rqwait(rpc, RQ_FINISH, &result) && !ipc_rqwait(rpc, 0, &result) &&
        (isize) result.words[0] == -ESTALE && result.words[1] == lease.seq && !result.words[2] &&
        !result.words[3] && !ipc_rqack(rpc, ACK_RESULT | ACK_RELEASE) &&
        !sysconnworkrelease(&backend_work, &cache, lease));
  closeat(&backend_work, bound, 1);
  query.words[0] = STOP;
  query.words[1] = query.words[2] = query.words[3] = 0;
  CHECK(!ipc_call(backend_work.state.call, &query, CALL_CONTROL, &result) && result.words[0] == 2 &&
        result.words[1] == 2 && !sysconninvalidate(&cache, backend_work.state.tag) &&
        !sysfileconnclean(&pool, &backend_work, &cache));
  if (timeout)
    CHECK(!ipc_call(work.state.call, &query, CALL_CONTROL, &result) && !result.words[0]);
  filecreatetrace[0] = 2;
}
static isize
revokepreparation(void *context, const Openprep *prep)
{
  u64 *closed = context;
  CHECK(prep->state == Openpending && prep->session && prep->reference);
  (*closed)++;
  return 0;
}
static void
backend(const Bootview *boot, u32 recover)
{
  Connpeer        clientpeer, vfspeer;
  Ipcmsg          input;
  Msg             reply;
  Resreply        transfer;
  Allocarg        alloc;
  Maparg          mapping;
  Fileslot        descriptor;
  Fileowner       preparer = {0};
  Filewatch       watch = {0};
  Handle          memory = 0, space = bootresource(boot, BOOT_SELF_SPACE), waiter = 0;
  Filehead       *head = (Filehead *) ADDRESS;
  Openprep        preps[4] = {{0}};
  const Openlimit limit = {4, 4};
  Openprep       *prep;
  u64             created = 0, closed = 0, token, i;
  u64             wanted, attached, aborted, cancelled = 0;
  u64             armed = 0, next, now, generation;
  isize           status;
  u32             retired;
  wanted = recover == 4 ? 1 : recover == 2 ? 2 : recover == 5 ? 3 : 4;
  attached = recover == 4 ? 1 : recover == 2 ? 2 : 3;
  aborted = recover == 4 || recover == 2 ? 1 : recover == 5 ? 2 : 3;
  CHECK(!sysconnpeer(bootresource(boot, 256), &clientpeer) &&
        !sysconnpeer(bootresource(boot, 257), &vfspeer));
  for (;;) {
    if (recover == 10) {
      next = sysopennext(preps, ARRAY_SIZE(preps));
      if (next != armed) {
        CHECK(!(next ? __sys_alarmset(next, &generation) : __sys_alarmcancel(&generation)));
        armed = next;
      }
    }
    CHECK(!ipc_recv(recover == 9 ? EVENT_LIFE : recover == 10 ? EVENT_TIMER : 0, 0, &input));
    if (recover == 10) {
      CHECK(!sysfilenow(&now));
      CHECK(
          !sysopensweep(preps, ARRAY_SIZE(preps), now, NULL, revokepreparation, &closed, &retired));
      cancelled += retired;
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
        CHECK(!sysfilenow(&now) && !sysopensweep(preps, ARRAY_SIZE(preps), now, &preparer,
                                                 revokepreparation, &closed, &retired));
        cancelled += retired;
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
      CHECK(!sysfileopendeadline(&next) &&
            !sysopenreserve(preps, ARRAY_SIZE(preps), limit, &preparer, clientpeer.pid,
                            clientpeer.image, input.words[1], input.words[2], next, &token));
      created++;
      reply.words[1] = token;
      CHECK(!ipc_reply(input.request, &reply));
      continue;
    }
    if (input.words[0] == ENDPEER) {
      CHECK((recover == 1 || recover == 6) && input.source == vfspeer.pid &&
            input.image == vfspeer.image && created == 1 && preps[0].state == Openpending &&
            !memory);
      filecreatetrace[1] = 1;
      filecreatetrace[2] = 0;
      filecreatetrace[4] = 1;
      filecreatetrace[0] = 2;
      return;
    }
    if (input.words[0] != FS_ABORT && input.words[0] != FS_ATTACH)
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
        CHECK(created == 2 && closed == 2 && cancelled == 1 && memory &&
              preps[0].state == Openclosed && preps[1].state == Opencancelled &&
              filecreatetrace[3] == 1 && filecreatetrace[4] == 1);
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
        CHECK(preps[i].state == Openclosed || preps[i].state == Opencancelled);
      reply.words[0] = created;
      reply.words[1] = closed;
      CHECK(!ipc_reply(input.request, &reply));
      break;
    }
    if (input.words[0] == FS_ABORT || input.words[0] == FS_ATTACH) {
      token = input.words[3];
      CHECK(!input.words[1] && input.words[2] && token);
      prep = sysopenlookup(preps, ARRAY_SIZE(preps), token);
      reply.words[1] = input.words[2];
      if (!prep) {
        reply.words[0] = (u64) -ESTALE;
        CHECK(!ipc_reply(input.request, &reply));
        continue;
      }
      if (recover == 3 && input.words[0] == FS_ABORT) {
        CHECK(created == 1 && prep->state == Openpending && sysopenabort(prep, &input) == 1 &&
              !memory);
        filecreatetrace[1] = 1;
        filecreatetrace[4] = 1;
        filecreatetrace[0] = 2;
        return;
      }
      if (input.words[0] == FS_ABORT) {
        status = sysopenabort(prep, &input);
        if (status == 1) {
          sysopenretire(prep);
          closed++;
        }
        if (status < 0)
          reply.words[0] = (u64) (status == -ERR_DENIED ? -EACCES : -ESTALE);
        else
          filecreatetrace[4]++;
        CHECK(!ipc_reply(input.request, &reply));
        continue;
      }
      CHECK(!sysfilenow(&now));
      status = sysopenattach(prep, &input, now);
      if (status) {
        reply.words[0] = (u64) (status == -ERR_DENIED ? -EACCES : -ESTALE);
        CHECK(!ipc_reply(input.request, &reply));
        continue;
      }
      filecreatetrace[3]++;
      if (recover == 7 || (recover == 8 && prep->session == 202)) {
        filecreatetrace[1] = created;
        filecreatetrace[4] = 1;
        filecreatetrace[0] = 2;
        return;
      }
      if (recover == 5 && prep->session == 202) {
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
        CHECK(prep->session == 203);
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
      sysopencommit(prep);
      continue;
    }
    CHECK(input.words[0] == FS_CLOSE && input.words[1] == CONNID && input.words[3] == 6);
    if (recover >= 9)
      CHECK(!sysfilewatchaccept(&watch, head, FS_CONN_BYTES, &input, &descriptor));
    else
      CHECK(!sysfilerequest(head, FS_CONN_BYTES, 6, input.words[2], &descriptor));
    CHECK(!sysfileargs(FS_CLOSE, &descriptor));
    prep = sysopenref(preps, ARRAY_SIZE(preps), &input, &descriptor);
    if (!prep)
      reply.words[0] = (u64) -ESTALE;
    else {
      if (recover >= 9)
        CHECK(prep == &preps[0] && cancelled == 1);
      sysopenretire(prep);
      closed++;
    }
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
  Fileref   reference;
  Conntag   next;
  Connentry entry;
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
  CHECK(first && second && first != second && sysfileconnreap(&pool, &work, &cache) == -ERR_STATE &&
        !sysfilerefdrop(&pool, work.state.self, first) &&
        sysfileclosepump(&pool, &work, &cache, 800, 0) == SYSFILE_CLOSE_SUBMITTED);
  rpc = work.state.operations[6].rpc;
  key.connection = work.state.tag;
  key.lease = work.state.operations[6].lease;
  key.owner = 0;
  CHECK(ipc_rqwait(rpc, 0, &result) == -ERR_PEER &&
        sysfileclosepump(&pool, &work, &cache, 800, 0) == -ERR_PEER &&
        !sysoppeerended(&work, &cache, &key) && work.state.ended == 1);
  CHECK(sysfileclosepump(&pool, &work, &cache, 800, 0) == -ERR_WOULD_BLOCK &&
        work.state.operations[6].rpc == rpc);
  filecreatetrace[5] = (u64) rpc;
  filecreatetrace[6] = 6;
  do {
    CHECK(++steps < 64 && !__sys_yield());
    error = sysfileclosepump(&pool, &work, &cache, 800, 0);
  } while (error == -ERR_WOULD_BLOCK);
  CHECK(error == SYSFILE_CLOSE_RECOVERED &&
        sysfilerefview(&pool, work.state.self, first, &reference) == -ERR_STALE &&
        !sysfilerefview(&pool, work.state.self, second, &reference) &&
        reference.state == SYSFILE_REF_LIVE && reference.aliases == 1 &&
        sysfileconnreap(&pool, &work, &cache) == -ERR_BUSY && work.state.map.mapped &&
        work.state.memory && work.state.call && !sysfilerefdrop(&pool, work.state.self, second) &&
        sysfileconnreap(&pool, &work, &cache) == -ERR_BUSY && work.state.map.mapped &&
        work.state.memory && work.state.call &&
        sysfileclosepump(&pool, &work, &cache, 800, 0) == SYSFILE_CLOSE_RECOVERED &&
        !sysfileconnreap(&pool, &work, &cache) && !work.state.map.mapped && !work.state.memory &&
        !work.state.call && !sysconnbind(&cache, work.state.self, &next) &&
        next.index == key.connection.index && next.generation > key.connection.generation &&
        sysfileconnreap(&pool, &work, &cache) == -ERR_STALE && !sysconnview(&cache, next, &entry) &&
        entry.state == SYSCONN_UNCLAIMED && !sysconnworkinit(&work, &cache, next));
  filecreatetrace[2] = 2;
  filecreatetrace[3] = 1;
  filecreatetrace[4] = steps;
  filecreatetrace[0] = 2;
}
static void
pipedeadclient(const Bootview *boot)
{
  Pipecreate pipe = {0};
  Fdtag      tags[2];
  Handle     rpc;
  isize      business = 123, error;
  int        pair[2] = {-1, -1};
  u32        i, steps = 0;
  setup(boot);
  CHECK(!sysctxinit(&contexts, work.state.self));
  CHECK(sysctxpipecreate(&contexts, 0, &pool, &work, &cache, &fds, &pipe, &active_key, pair,
                         &business) == -ERR_WOULD_BLOCK &&
        !active_key.sequence && work.state.ended == 1 && !sysfdcheckpair(&fds, pipe.tags) &&
        pair[0] == -1 && pair[1] == -1 && business == 123);
  rpc = work.state.operations[pipe.operation.lease.slot].rpc;
  CHECK(rpc);
  filecreatetrace[5] = (u64) rpc;
  filecreatetrace[6] = 6;
  do {
    CHECK(++steps < 64 && !__sys_yield());
    error = sysctxpipecreate(&contexts, 0, &pool, &work, &cache, &fds, &pipe, &active_key, pair,
                             &business);
  } while (error == -ERR_WOULD_BLOCK);
  CHECK(error == -ERR_PEER && !pipe.operation.lease.seq && !active_key.sequence && !business &&
        pair[0] == -1 && pair[1] == -1 && !sysfdreservepair(&fds, tags) && tags[0].index == 0 &&
        tags[1].index == 1 && !sysfdcancelpair(&fds, tags));
  for (i = 0; i < SYSFILE_REFS; i++)
    CHECK(pool.entries[i].state == SYSFILE_REF_FREE ||
          pool.entries[i].state == SYSFILE_REF_RETIRED);
  CHECK(!sysfileconnreap(&pool, &work, &cache));
  filecreatetrace[1] = 1;
  filecreatetrace[2] = 2;
  filecreatetrace[3] = 1;
  filecreatetrace[4] = steps;
  filecreatetrace[7] = 1;
  filecreatetrace[8] = (u64) error;
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
  if (opcode == FS_PIPE) {
    pipedeadclient(boot);
    return;
  }
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
  CHECK(!sysfileconnreap(&pool, &work, &cache));
  filecreatetrace[2] = 2;
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
  Opencreate open = {0};
  Opkey      key;
  Connpeer   peer;
  Conntag    tag;
  Connentry  entry;
  Fdview     fd;
  Handle     rpc;
  Rkey       invocation = {0};
  Msg        message;
  Ipcmsg     result;
  u32        i, reserved, steps = 0;
  int        number = -2;
  isize      error, business = 9;
  setup(boot);
  CHECK(!sysctxinit(&contexts, work.state.self));
  CHECK(sysctxopencreate(&contexts, 0, &pool, &work, &cache, &fds, "/bound", 0, 0, 0, &open,
                         &invocation, &number, &business) == 1);
  filecreatetrace[1]++;
  key = open.operation;
  reserved = open.tag.index;
  peer = work.state.creators[key.lease.slot].peer;
  for (i = 0; i < SYSCONN_COUNT - 1; i++) {
    peer.image++;
    CHECK(!sysconnbind(&cache, peer, &tag));
  }
  peer = work.state.creators[key.lease.slot].peer;
  CHECK(sysfilecreatebackend(&pool, &work, &cache, &key, &tag) == -ERR_CAPACITY);
  message.words[0] = ENDPEER;
  message.words[1] = message.words[2] = message.words[3] = 0;
  if (!submitted) {
    CHECK(!ipc_call(work.state.call, &message, CALL_ASYNC | CALL_CONTROL, &result));
    rpc = result.request;
    CHECK(!ipc_rqwait(rpc, 0, &result) && !result.words[0] &&
          !ipc_rqack(rpc, ACK_RESULT | ACK_RELEASE));
  }
  error = sysctxopenrevoke(&contexts, 0, &pool, &work, &cache, &key, 0, &invocation);
  if (submitted) {
    CHECK(error == -ERR_WOULD_BLOCK && !invocation.sequence &&
          sysfileopendiscard(&pool, &work, &cache, &fds, &open) == -ERR_STATE &&
          !sysfdobserve(&fds, reserved, &fd) && fd.tag.state == SYSFD_RESERVED);
    filecreatetrace[5] = (u64) work.state.bootstrap.rpc;
    filecreatetrace[6] = 6;
    do {
      CHECK(++steps < 64 && !__sys_yield());
      error = sysctxopenrevoke(&contexts, 0, &pool, &work, &cache, &key, 0, &invocation);
    } while (error == -ERR_WOULD_BLOCK);
  }
  CHECK(!error && !invocation.sequence && !sysfileopendiscard(&pool, &work, &cache, &fds, &open) &&
        !sysfdobserve(&fds, reserved, &fd) && fd.tag.state == SYSFD_FREE && !work.state.ended &&
        !sysconnview(&cache, work.state.tag, &entry) && entry.state == SYSCONN_READY &&
        sysconnbind(&cache, peer, &tag) == -ERR_CAPACITY);
  message.words[0] = STOP;
  CHECK(!ipc_call(work.state.call, &message, 0, &result) && !result.words[0] &&
        !sysconninvalidate(&cache, work.state.tag) && !sysfileconnclean(&pool, &work, &cache));
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
  Opencreate open = {0};
  Rkey       opening = {0};
  const u64  args[8] = {0, 0, 0, 3};
  Msg        message;
  Fdview     fd;
  Opkey      failed, success;
  Fileclose  anchor;
  Fileref    held;
  Rkey       invocation;
  u64        file, pin = 0, copied = IO_GUARD, steps = 0;
  isize      outcome, error;
  int        number = -2;
  setup(boot);
  CHECK(!sysctxinit(&contexts, work.state.self));
  sysfdinit(&fds);
  CHECK(!sysfileopenprepare(&pool, &work, &cache, &fds, 0, 99, args, "/a", 3, BOOT_FD_CLOEXEC,
                            &open, &message) &&
        !sysfdobserve(&fds, 0, &fd) && fd.tag.state == SYSFD_RESERVED && !fd.file &&
        !sysfileopenabort(&pool, &work, &cache, &fds, &open) && !sysfdobserve(&fds, 0, &fd) &&
        fd.tag.state == SYSFD_FREE);
  CHECK(!sysctxopencreate(&contexts, 0, &pool, &work, &cache, &fds, "/a", 0, 0, BOOT_FD_CLOEXEC,
                          &open, &opening, &number, &outcome) &&
        !number && !outcome && !open.operation.lease.seq && !opening.sequence &&
        !sysfdobserve(&fds, 0, &fd) && fd.flags == BOOT_FD_CLOEXEC);
  filecreatetrace[1]++;
  file = fd.file;
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
        !sysfddetach(&fds, fd.tag, &fd) && !sysfilerefdrop(&pool, work.state.self, file) &&
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
        !sysfileconnclean(&pool, &work, &cache));
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
  CHECK(!sysfileconnclean(&pool, &backend_work, &cache) &&
        !sysconnview(&cache, work.state.tag, &entry) && entry.state == SYSCONN_READY);
  zero(&message, sizeof(message));
  message.words[0] = STOP;
  CHECK(!ipc_call(work.state.call, &message, 0, &result) && !result.words[0] &&
        !sysconninvalidate(&cache, work.state.tag) && !sysfileconnclean(&pool, &work, &cache));
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
        !(mode == 2 ? sysfileconnclean(&pool, &work, &cache) : sysconnworkclean(&work, &cache)));
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
    CHECK(sysfilecreateattachdiscard(&pool, &work, &cache, &key, &backend_work, attach, space,
                                     &mapped) == -ERR_STATE);
    CHECK(!sysfilecreaterevokeprepare(&pool, &work, &cache, &key, 0, 0, &abort, &call));
    bootstrap(&work.state.bootstrap, abort, call, CALL_CONTROL);
    CHECK(!sysfilecreaterevoked(&pool, &work, &cache, &key, abort));
    CHECK(!sysfilecreateattachdiscard(&pool, &work, &cache, &key, &backend_work, attach, space,
                                      &mapped));
    zero(files, sizeof(files));
    CHECK(!sysfilecreatefinish(&pool, &work, &cache, &key, files, &source) && !files[0] &&
          !source && !backend_work.state.bootstrap.state && !backend_work.state.ended &&
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
        !sysconninvalidate(&cache, tag) && !sysfileconnclean(&pool, &backend_work, &cache) &&
        !ipc_call(work.state.call, &message, 0, &result) && !result.words[0] &&
        !sysconninvalidate(&cache, work.state.tag) && !sysfileconnclean(&pool, &work, &cache));
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
  retry = attach;
  retry.owner++;
  CHECK(sysfilecreateattachdiscard(&pool, &work, &cache, &key, &backend_work, retry, space,
                                   &mapped) == -ERR_STALE &&
        mapped.mapped && backend_work.state.bootstrap.resource == memory &&
        backend_work.state.call == retained &&
        !sysfilecreateattachdiscard(&pool, &work, &cache, &key, &backend_work, attach, space,
                                    &mapped));
  zero(files, sizeof(files));
  CHECK(!sysfilecreatefinish(&pool, &work, &cache, &key, files, &source) && !files[0] && !source &&
        !mapped.mapped && !backend_work.state.bootstrap.state && !backend_work.state.ended &&
        backend_work.state.call == retained && !sysconnview(&cache, tag, &entry) &&
        entry.state == SYSCONN_DRAINING && !entry.owner &&
        sysconnbegin(&cache, tag, 999, &retry) == -ERR_PEER &&
        __sys_copy(&copied, (const void *) (ADDRESS + 0x10000), sizeof(copied)) == -ERR_FAULT &&
        __sys_free(memory) == -ERR_BAD_HANDLE && !sysconnpeer(retained, &actual) &&
        actual.pid == peer.pid && actual.image == peer.image);
  zero(&message, sizeof(message));
  message.words[0] = STOP;
  CHECK(!ipc_call(retained, &message, 0, &result) && result.words[0] == 1 && result.words[1] == 1 &&
        !sysfileconnclean(&pool, &backend_work, &cache) &&
        !ipc_call(work.state.call, &message, 0, &result) && !result.words[0] &&
        !sysconninvalidate(&cache, work.state.tag) && !sysfileconnclean(&pool, &work, &cache));
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
        !sysfileconnclean(&pool, &backend_work, &cache));
  CHECK(!ipc_call(work.state.call, &message, 0, &result) && !result.words[0] &&
        !sysconninvalidate(&cache, work.state.tag) && !sysfileconnclean(&pool, &work, &cache));
  filecreatetrace[2] = 2;
  filecreatetrace[3] = 1;
  filecreatetrace[0] = 2;
}
int
main(int argc, char **argv)
{
  CHECK(argc == 1 &&
        ((argv[0][0] >= '0' && argv[0][0] <= '9') || (argv[0][0] >= 'a' && argv[0][0] <= 'z') ||
         (argv[0][0] >= 'A' && argv[0][0] <= 'S')) &&
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
  else if (argv[0][0] == 'R')
    claimsource(sysboot());
  else if (argv[0][0] == 'S')
    claimbackend(sysboot());
  else
    backend(sysboot(), argv[0][0] == 'w' ? 6 : argv[0][0] == 'z' ? 7 : 8);
  return 42;
}
