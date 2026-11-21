#include <errno.h>
#include <signal.h>
#include <sys/boot.h>
#include <sys/channel.h>
#include <unistd.h>
#include "private/close.h"
#include "private/connection.h"
#include "private/contextipc.h"
#include "private/filecreate.h"
#include "private/fileimage.h"
#include "private/filewatch.h"
#include "private/sigentry.h"
#include "private/start.h"
#include "private/syscall.h"
#include "../../sbin/fs/pipefile.h"
#include "../../sbin/fs/serve.h"

#define ADDRESS  0x1600000UL
#define PIPETEST 0xf008

volatile u64 pipeiotrace[2];

#define CHECK(c)                                                                                   \
  do {                                                                                             \
    if (!(c)) {                                                                                    \
      pipeiotrace[1] = __LINE__;                                                                   \
      __sys_exit(191);                                                                             \
    }                                                                                              \
  } while (0)

static void
setup(const Bootview *boot, Connwork *work, Conncache *cache, Rbank *bank)
{
  Connpeer  peer;
  Conntag   tag;
  Connclaim claim;
  Bootmap   map = {0};
  Rkey      active = {0};
  Handle    call, parent, space, redundant = 0;
  CHECK(!__sys_getid(&cache->self.pid, &cache->self.image, &parent));
  CHECK(!sysconninit(cache, cache->self));
  call = bootresource(boot, BOOT_FS_CALL);
  CHECK(!sysconnpeer(call, &peer) && !sysconnbind(cache, peer, &tag) &&
        !sysconnworkinit(work, cache, tag) &&
        !sysconnworktakecall(work, cache, &call, &redundant) && !call && !redundant &&
        !sysconnbegin(cache, tag, 1, &claim) &&
        !sysbootprepare(&work->state.bootstrap, claim, FS_HELLO, UINT64_MAX, 0) &&
        !sysctxinit(bank, work->state.self));
  space = bootresource(boot, BOOT_SELF_SPACE);
  /* A conflicting image page leaves the already delivered HELLO retryable. */
  CHECK(sysctxhello(bank, 0, work, cache, space, (uintptr_t) pipeiotrace & ~PAGE_MASK, &map,
                    &active) < 0 &&
        !active.sequence && !map.mapped && !work->state.memory &&
        work->state.bootstrap.state == SYSBOOT_RESULT && work->state.bootstrap.resource);
  CHECK(!sysctxhello(bank, 0, work, cache, space, ADDRESS, &map, &active) && !map.mapped &&
        !active.sequence && work->state.map.verified &&
        work->state.bootstrap.state == SYSBOOT_FREE);
}

static void
filelifetime(Handle pid, u64 image)
{
  static Pipefile files[1];
  Piperef         refs[3] = {{0}};
  Pipetable       table = {files, refs, ARRAY_SIZE(files), ARRAY_SIZE(refs), 0};
  Pipekey         keys[2], later[2], duplicate;
  Pipefile       *pinned;
  u8              byte = 'z';
  CHECK(!pipefilecreate(&table, pid, image, 1, keys) && keys[0].session != keys[1].session &&
        keys[0].reference != keys[1].reference &&
        pipefilepin(&table, pid, image + 1, keys[0].session, PIPE_READ, &pinned) == -ERR_STALE &&
        pipefilepin(&table, pid, image, keys[0].session, PIPE_WRITE, &pinned) == -ERR_DENIED &&
        !pipefileclose(&table, pid, image, keys[PIPE_WRITE]));
  /* Reuse the only stream, then keep the writer alive with an admitted I/O. */
  CHECK(!pipefileclose(&table, pid, image, keys[PIPE_READ]) &&
        !pipefilecreate(&table, pid, image, 0, later) && later[0].session > keys[1].session &&
        pipefileclose(&table, pid, image, keys[PIPE_READ]) == -ERR_STALE);
  duplicate.session = later[PIPE_WRITE].session;
  CHECK(!pipefiledup(&table, pid, image, later[PIPE_WRITE], &duplicate.reference) &&
        pipefiledup(&table, pid, image, later[PIPE_WRITE], &keys[0].reference) == -ERR_CAPACITY &&
        !pipefileclose(&table, pid, image, later[PIPE_WRITE]) &&
        !pipefilepin(&table, pid, image, duplicate.session, PIPE_WRITE, &pinned) &&
        !pinned->nonblock[PIPE_WRITE] && !pipefileclose(&table, pid, image, duplicate) &&
        pipefilepin(&table, pid, image, duplicate.session, PIPE_WRITE, &pinned) == -ERR_STALE &&
        piperead(&pinned->stream, &byte, 1) == -ERR_WOULD_BLOCK &&
        !pipedrop(&pinned->stream, PIPE_WRITE) && piperead(&pinned->stream, &byte, 1) == 0);
  CHECK(!pipefileretire(&table, pid, image + 1) &&
        !pipefilepin(&table, pid, image, later[PIPE_READ].session, PIPE_READ, &pinned) &&
        !pipedrop(&pinned->stream, PIPE_READ));
  CHECK(!pipefileretire(&table, pid, image) && !pipefilecreate(&table, pid, image, 0, keys) &&
        !pipefileretire(&table, pid, image));
}

static void
server(const Bootview *boot)
{
  static u8       fill[PIPE_CAPACITY];
  static Pipefile files[1];
  static Pipewait waits[2];
  Piperef         refs[3] = {{0}};
  Pipetable       table = {files, refs, ARRAY_SIZE(files), ARRAY_SIZE(refs), 0};
  Pipedispatch    dispatch = {&table, waits, ARRAY_SIZE(waits)};
  Pipekey         keys[2];
  Pipe           *pipe = &files[0].stream;
  Filelink        link = {0};
  Ipcmsg          input;
  u32             attempt = 0, phase = 0, i;
  CHECK(!ipc_recv(0, 0, &input) &&
        !sysfilehello(&link, 1, bootresource(boot, 256), bootresource(boot, BOOT_SELF_ALLOC),
                      bootresource(boot, BOOT_SELF_SPACE), ADDRESS, &input));
  filelifetime(link.watch.owner.pid, link.watch.owner.image);
  for (;;) {
    CHECK(!ipc_recv(EVENT_RQ_CANCEL | EVENT_LIFE, 0, &input));
    if (input.kind == IPC_EVENT) {
      if (input.words[0] & EVENT_LIFE) {
        CHECK(!sysfilelinksweep(&link, 1, pipedrain, &dispatch));
        if (!link.ready) {
          CHECK(attempt == 8 && phase == 8 && !pipe->refs[0] && !pipe->refs[1]);
          for (i = 0; i < ARRAY_SIZE(waits); i++)
            CHECK(!waits[i].io.request);
          return;
        }
      }
      CHECK(!pipepump(&dispatch));
      continue;
    }
    CHECK(input.kind == IPC_CALL && input.source == link.watch.owner.pid &&
          input.image == link.watch.owner.image);
    if (input.words[0] == PIPETEST) {
      Msg reply = {{0, 0, 0, 0}};
      CHECK(input.words[1] == phase);
      switch (phase++) {
      case 0:
      case 2:
        CHECK(waits[0].io.request && !waits[0].io.fixed);
        break;
      case 1:
        CHECK(!pipepump(&dispatch) && !waits[0].io.request && !pipe->bytes);
        files[0].nonblock[PIPE_READ] = 1;
        break;
      case 3:
        CHECK(waits[0].io.request && *waits[0].io.page == 'x' &&
              piperead(pipe, fill, sizeof(fill)) == sizeof(fill) && !pipepump(&dispatch) &&
              !waits[0].io.request && pipe->bytes == 1);
        break;
      case 4:
        CHECK(attempt == 6 && !pipe->bytes && waits[0].io.request);
        break;
      case 5:
        CHECK(attempt == 6 && !pipefilecreate(&table, input.source, input.image, 0, keys) &&
              !pipefileclose(&table, input.source, input.image, keys[PIPE_READ]));
        reply.words[2] = keys[PIPE_WRITE].session;
        break;
      case 6:
        CHECK(attempt == 7 && !waits[0].io.request && !pipe->bytes && pipe->refs[PIPE_WRITE] == 1);
        CHECK(!pipefileclose(&table, input.source, input.image, keys[PIPE_WRITE]) &&
              !pipefilecreate(&table, input.source, input.image, 0, keys));
        reply.words[2] = keys[PIPE_READ].session;
        break;
      case 7:
        CHECK(attempt == 8 && waits[0].io.request && pipe->refs[PIPE_READ] == 2);
        break;
      default:
        CHECK(0);
      }
      CHECK(!ipc_reply(input.request, &reply));
      continue;
    }
    if (input.words[0] == FS_READ || input.words[0] == FS_WRITE) {
      if (attempt == 3)
        CHECK(pipewrite(pipe, fill, sizeof(fill)) == sizeof(fill));
      if (++attempt <= 7)
        pipeiotrace[0] = attempt;
    }
    CHECK(!pipedispatch(&dispatch, &link, 1, &input) && !pipepump(&dispatch));
    if (attempt == 2)
      files[0].nonblock[PIPE_READ] = 0;
  }
}

static u64
refcall(Connwork *work, Conncache *cache, u64 opcode, Pipekey key)
{
  Fileslot  slot = {0};
  Filelease lease;
  Ipcmsg    result;
  Msg       request = {{opcode, work->state.map.connid, 0, 0}};
  CHECK(!sysconnworkreserve(work, cache, SYSFILE_CONTROL, 1, &lease));
  request.words[2] = slot.seq = lease.seq;
  request.words[3] = lease.slot;
  slot.state = FS_SLOT_SUBMITTED;
  slot.span = 1;
  slot.object = key.session;
  slot.args[0] = key.reference;
  CHECK(
      !__sys_copy((void *) (work->state.map.address + FS_DESC_OFFSET + lease.slot * FS_DESC_BYTES),
                  &slot, sizeof(slot)) &&
      !ipc_call(work->state.call, &request, CALL_CONTROL, &result) && !result.words[0] &&
      result.words[1] == lease.seq && !result.words[3] && !sysconnworkrelease(work, cache, lease));
  return result.words[2];
}

static void
createpipe(Connwork *work, Conncache *cache, Pipekey keys[2])
{
  Fileslot      slot = {0};
  Filelease     lease;
  Ipcmsg        result;
  Msg           call;
  Handle        rpc;
  volatile u64 *created;
  slot.state = FS_SLOT_SUBMITTED;
  slot.span = 1;
  CHECK(!sysconnworkreserve(work, cache, SYSFILE_MAIN, 1, &lease));
  call.words[0] = FS_PIPE;
  call.words[1] = work->state.map.connid;
  call.words[2] = slot.seq = lease.seq;
  call.words[3] = lease.slot;
  slot.args[1] = PIPE_CAPACITY;
  CHECK(
      !__sys_copy((void *) (work->state.map.address + FS_DESC_OFFSET + lease.slot * FS_DESC_BYTES),
                  &slot, sizeof(slot)) &&
      !ipc_call(work->state.call, &call, CALL_ASYNC, &result));
  rpc = result.request;
  CHECK(!ipc_rqwait(rpc, 0, &result) && !result.words[0] && result.words[1] == lease.seq &&
        result.words[2] == 2 && result.words[3] == 32 && !ipc_rqwait(rpc, RQ_FINISH, &result));
  created =
      (volatile u64 *) (work->state.map.address + FS_DATA_OFFSET + lease.slot * FS_SLOT_BYTES);
  keys[PIPE_READ].session = created[0];
  keys[PIPE_READ].reference = created[1];
  keys[PIPE_WRITE].session = created[2];
  keys[PIPE_WRITE].reference = created[3];
  CHECK(keys[0].session && keys[1].session && keys[0].session != keys[1].session &&
        keys[0].reference && keys[1].reference && keys[0].reference != keys[1].reference &&
        !ipc_rqack(rpc, ACK_RESULT | ACK_RELEASE) && !sysconnworkrelease(work, cache, lease));
}

static void
serviceloop(const Bootview *boot, u32 interrupt)
{
  static Filelink link;
  static Pipefile file;
  static Pipewait wait;
  static Piperef  refs[2];
  Pipetable       table = {&file, refs, 1, ARRAY_SIZE(refs), 0};
  Fsservice       fs = {bootresource(boot, 256),
                        bootresource(boot, BOOT_SELF_ALLOC),
                        bootresource(boot, BOOT_SELF_SPACE),
                        ADDRESS,
                        &link,
                        1,
                        {&table, &wait, 1}};
  if (interrupt) {
    Ipcmsg request, gate;
    Sigsel target = {SIGNAL_SELECT_PID, 0};
    Msg    reply = {{0, 0, 0, 0}};
    CHECK(!fsstep(&fs) && link.ready && !ipc_recv(0, 0, &request) && request.kind == IPC_CALL &&
          request.words[0] == FS_PIPE && !ipc_defer(request.request));
    target.target = request.source;
    CHECK(!__sys_sigsend(&target, SIGNAL_USR1) && !ipc_recv(0, 0, &gate) && gate.kind == IPC_CALL &&
          gate.source == target.target && gate.words[0] == PIPETEST && !table.serial &&
          !ipc_reply(gate.request, &reply) && !pipedispatch(&fs.pipes, &link, 1, &request) &&
          table.serial == 4);
  }
  do {
    CHECK(!fsstep(&fs));
  } while (!table.serial || link.ready);
  CHECK(!link.memory && !link.address && !link.watch.owner.cookie && !wait.io.request &&
        !file.stream.refs[0] && !file.stream.refs[1]);
  pipeiotrace[0] = 7;
}

static volatile int caught;
static Fileimage   *interruptedimage;
static Connwork    *interruptedwork;

static void
oninterrupt(int signal)
{
  Fileimage *image;
  Connwork  *work;
  Fdview     readend, writeend;
  if (signal == SIGUSR1) {
    CHECK(!sysfileimage(&image) && image == interruptedimage && !sysfileimagefs(1, 0, &work) &&
          work == interruptedwork && !sysfdobserve(&image->fds, 0, &readend) &&
          readend.tag.state == SYSFD_RESERVED && !sysfdobserve(&image->fds, 1, &writeend) &&
          writeend.tag.state == SYSFD_RESERVED && dup(0) == -1 && errno == EBADF);
    caught++;
  }
}

static void
interruptclient(void)
{
  Fileimage       *image;
  Connwork        *work = NULL;
  Conncache       *cache;
  struct sigaction action = {0};
  Pipecreate       pipe = {0};
  Rkey             active = {0};
  Msg              gate = {{PIPETEST, 0, 0, 0}};
  Ipcmsg           result;
  Handle           rpc;
  isize            business;
  int              fds[2] = {-1, -1};
  int              fd;
  Fdview           original, alias, displaced;
  Fileref          ref;
  u32              turns = 0;
  isize            progress;
  CHECK(!sysfileimage(&image));
  cache = sysfileimagecache(image);
  CHECK(sysfileimagefs(0, (uintptr_t) pipeiotrace & ~PAGE_MASK, &work) < 0 && !work &&
        image->connections[0].state.bootstrap.state == SYSBOOT_RESULT &&
        image->connections[0].state.bootstrap.resource && !sysfileimagefs(0, ADDRESS, &work) &&
        work == &image->connections[0]);
  interruptedimage = image;
  interruptedwork = work;
  action.sa_handler = oninterrupt;
  CHECK(!sigaction(SIGUSR1, &action, NULL));
  errno = EBUSY;
  CHECK(sysctxpipecreate(&image->contexts, 0, &image->files, work, cache, &image->fds, &pipe,
                         &active, fds, &business) == -ERR_INTERRUPTED &&
        caught == 1 && errno == EBUSY && !active.sequence &&
        !sysoprpc(work, cache, &pipe.operation, &rpc) && rpc &&
        !sysfdcheckpair(&image->fds, pipe.tags) && fds[0] == -1 && fds[1] == -1 &&
        !ipc_call(work->state.call, &gate, CALL_CONTROL, &result));
  CHECK(!sysctxpipecreate(&image->contexts, 0, &image->files, work, cache, &image->fds, &pipe,
                          &active, fds, &business) &&
        !pipe.operation.lease.seq && !active.sequence && !business && fds[0] == 0 && fds[1] == 1 &&
        caught == 1);
  CHECK(!sysfdobserve(&image->fds, fds[0], &original) &&
        !sysfdsetflags(&image->fds, original.tag, BOOT_FD_CLOEXEC));
  for (fd = 2; fd < BOOT_FD_COUNT; fd++)
    CHECK(dup(fds[0]) == fd && errno == EBUSY && !sysfdobserve(&image->fds, fd, &alias) &&
          alias.file == original.file && !alias.flags);
  CHECK(dup(fds[0]) == -1 && errno == EMFILE && dup(-1) == -1 && errno == EBADF &&
        !sysfdobserve(&image->fds, fds[0], &original) && original.flags == BOOT_FD_CLOEXEC &&
        !sysfilerefview(&image->files, work->state.self, original.file, &ref) &&
        ref.aliases == BOOT_FD_COUNT - 1);
  CHECK(dup2(fds[0], fds[0]) == fds[0] && !sysfdobserve(&image->fds, fds[0], &alias) &&
        alias.flags == original.flags && alias.tag.generation == original.tag.generation &&
        !sysfdobserve(&image->fds, fds[1], &displaced) &&
        !sysfdsetflags(&image->fds, displaced.tag, BOOT_FD_CLOEXEC) && dup2(-1, fds[1]) == -1 &&
        errno == EBADF && !sysfdobserve(&image->fds, fds[1], &alias) &&
        alias.file == displaced.file && alias.flags == BOOT_FD_CLOEXEC &&
        dup2(fds[0], fds[1]) == fds[1] && !sysfdobserve(&image->fds, fds[1], &alias) &&
        alias.file == original.file && !alias.flags &&
        alias.tag.generation > displaced.tag.generation && work->state.operations[6].rpc &&
        work->state.operations[6].file == displaced.file);
  do {
    progress = sysfileimageclosework(work->state.tag);
    if (progress == -ERR_WOULD_BLOCK)
      CHECK(++turns < 64 && !__sys_yield());
  } while (progress == -ERR_WOULD_BLOCK);
  CHECK(!progress && !work->state.operations[6].rpc &&
        sysfilerefview(&image->files, work->state.self, displaced.file, &ref) == -ERR_STALE &&
        !sysfilerefview(&image->files, work->state.self, original.file, &ref) &&
        ref.aliases == BOOT_FD_COUNT);
  pipeiotrace[0] = 7;
}

static void
submitpipe(Rbank *bank, Connwork *work, Conncache *cache, Filepool *pool, Fdtable *table,
           Pipecreate *pipe, u64 owner)
{
  Msg      message;
  Rkey     active = {0};
  Handle   rpc;
  Connpeer self = work->state.self;
  CHECK(!sysfilepipeprepare(pool, work, cache, table, SYSFILE_MAIN, owner, pipe, &message) &&
        sysctxpipeio(bank, self, 1, pool, work, cache, &pipe->operation, &active) == -ERR_INVALID &&
        !active.sequence &&
        !sysctxpipeio(bank, self, 0, pool, work, cache, &pipe->operation, &active) &&
        !active.sequence && !sysoprpc(work, cache, &pipe->operation, &rpc));
  CHECK(!sysctxpipeio(bank, self, 0, pool, work, cache, &pipe->operation, &active) &&
        !active.sequence && work->state.operations[pipe->operation.lease.slot].rpc == rpc &&
        sysctxpipeio(bank, self, 0, pool, work, cache, &pipe->operation, &active) == 1);
}

static void
closepending(Connwork *work, Conncache *cache, Filepool *pool, u64 owner)
{
  Ipcmsg result;
  isize  error;
  CHECK(sysfileclosepump(pool, work, cache, owner, 0) == SYSFILE_CLOSE_SUBMITTED);
  do {
    error = sysfileclosepump(pool, work, cache, owner, 0);
    if (error == -ERR_WOULD_BLOCK)
      CHECK(!ipc_recv(EVENT_RQ_DONE, RECV_EVENTS_ONLY, &result));
  } while (error == -ERR_WOULD_BLOCK);
  CHECK(!error);
}

static void
serviceclient(const Bootview *boot)
{
  static Connwork  work;
  static Conncache cache;
  static Fdtable   table;
  static Filepool  pool;
  static Rbank     bank;
  Fdtag            held;
  Pipecreate       pipe = {0};
  Fdview           view;
  u64              closing;
  isize            business;
  int              fd[2] = {-1, -1};
  Handle           call = bootresource(boot, BOOT_FS_CALL);
  Handle           rpc;
  Msg              invalid = {{FS_HELLO, 0, 23, 0}};
  Ipcmsg           result;
  CHECK(!ipc_call(call, &invalid, 0, &result) && (isize) result.words[0] == -EINVAL &&
        result.words[1] == 23);
  setup(boot, &work, &cache, &bank);
  invalid.words[3] = UINT64_MAX;
  CHECK(!ipc_call(work.state.call, &invalid, 0, &result) && (isize) result.words[0] == -EBUSY &&
        result.words[1] == 23);
  invalid.words[0] = FS_ATTACH;
  CHECK(!ipc_call(work.state.call, &invalid, 0, &result) && (isize) result.words[0] == -ENOSYS);
  CHECK(!sysfilerefinit(&pool, work.state.self) && !sysfdreserve(&table, 1, &held) &&
        !sysfilepipeprepare(&pool, &work, &cache, &table, SYSFILE_MAIN, 99, &pipe, &invalid) &&
        !sysfilepipeabort(&pool, &work, &cache, &table, &pipe) && !pipe.operation.lease.seq);
  CHECK(!sysfilepipeprepare(&pool, &work, &cache, &table, SYSFILE_MAIN, 98, &pipe, &invalid) &&
        !ipc_call(work.state.call, &invalid, CALL_ASYNC, &result));
  rpc = result.request;
  CHECK(!sysopsubmitted(&work, &cache, &pipe.operation, &result.request) &&
        !sysopcancel(&work, &cache, &pipe.operation) && !ipc_rqwait(rpc, RQ_FINISH, &result) &&
        sysfilepipefinish(&pool, &work, &cache, &table, &pipe, fd, &business) == -ERR_CANCELLED &&
        !business && !pipe.operation.lease.seq && fd[0] == -1 && fd[1] == -1 &&
        !sysfilerefconnection(&pool, work.state.self, work.state.tag));
  submitpipe(&bank, &work, &cache, &pool, &table, &pipe, 100);
  CHECK(sysfilepipefinish(&pool, &work, &cache, &table, &pipe, (int *) 1, &business) ==
            -ERR_FAULT &&
        !business && !pipe.operation.lease.seq && !sysfdobserve(&table, 0, &view) &&
        view.tag.state == SYSFD_FREE && !sysfdobserve(&table, 2, &view) &&
        view.tag.state == SYSFD_FREE);
  closepending(&work, &cache, &pool, 101);
  closepending(&work, &cache, &pool, 102);
  CHECK(!sysfilerefconnection(&pool, work.state.self, work.state.tag));
  submitpipe(&bank, &work, &cache, &pool, &table, &pipe, 103);
  CHECK(!sysfilepipefinish(&pool, &work, &cache, &table, &pipe, fd, &business) && !business &&
        fd[0] == 0 && fd[1] == 2);
  submitpipe(&bank, &work, &cache, &pool, &table, &pipe, 104);
  CHECK(!sysfilepipefinish(&pool, &work, &cache, &table, &pipe, fd, &business) &&
        business == -ENFILE && fd[0] == 0 && fd[1] == 2 && !pipe.operation.lease.seq &&
        !sysfdobserve(&table, 3, &view) && view.tag.state == SYSFD_FREE &&
        !sysfdobserve(&table, fd[0], &view) && view.tag.state == SYSFD_LIVE && !view.flags &&
        !sysfdobserve(&table, fd[1], &view) && view.tag.state == SYSFD_LIVE && !view.flags &&
        !sysfdclose(&table, &pool, work.state.self, view.tag, &closing) && closing == view.file);
  closepending(&work, &cache, &pool, 105);
  /* Leave the read reference to the service's LIFE path. */
  pipeiotrace[0] = 7;
}

static void
run(const Bootview *boot)
{
  const isize      status[] = {-EINTR, -EAGAIN, -ESPIPE, 0, 0, 0, -EPIPE};
  static Connwork  work;
  static Conncache cache;
  static Rbank     bank;
  Fileslot         slot = {0};
  Filelease        lease;
  Ipcmsg           result;
  Msg              call = {{FS_READ, 0, 0, 0}}, gate = {{PIPETEST, 0, 0, 0}};
  volatile u8     *page;
  Handle           rpc;
  u32              attempt;
  Pipekey          keys[2], dup;
  setup(boot, &work, &cache, &bank);
  createpipe(&work, &cache, keys);
  slot.state = FS_SLOT_SUBMITTED;
  slot.span = 1;
  dup.session = keys[PIPE_WRITE].session;
  dup.reference = refcall(&work, &cache, FS_DUPREF, keys[PIPE_WRITE]);
  CHECK(dup.reference && dup.reference != keys[PIPE_WRITE].reference &&
        !refcall(&work, &cache, FS_CLOSE, keys[PIPE_WRITE]));
  keys[PIPE_WRITE] = dup;
  slot.payload_bytes = FS_SLOT_BYTES;
  slot.args[1] = 1;
  slot.args[2] = FS_SLOT_BYTES - 1;
  for (attempt = 0; attempt < ARRAY_SIZE(status); attempt++) {
    if (attempt == 6) {
      CHECK(!refcall(&work, &cache, FS_CLOSE, keys[PIPE_READ]));
      CHECK(!ipc_call(work.state.call, &gate, CALL_CONTROL, &result));
      keys[PIPE_WRITE].session = result.words[2];
      gate.words[1]++;
    }
    CHECK(!sysconnworkreserve(&work, &cache, SYSFILE_MAIN, 1, &lease));
    call.words[0] = attempt == 3 || attempt == 6 ? FS_WRITE : FS_READ;
    call.words[1] = work.state.map.connid;
    slot.object = keys[call.words[0] == FS_READ ? PIPE_READ : PIPE_WRITE].session;
    slot.seq = call.words[2] = lease.seq;
    call.words[3] = lease.slot;
    slot.args[3] = attempt == 2 ? FS_IO_POSITIONAL : 0;
    page = (volatile u8 *) (work.state.map.address + FS_DATA_OFFSET + lease.slot * FS_SLOT_BYTES);
    page[FS_SLOT_BYTES - 2] = 0xa5;
    page[FS_SLOT_BYTES - 1] = attempt == 3 ? 'p' : 0xa5;
    CHECK(
        !__sys_copy((void *) (work.state.map.address + FS_DESC_OFFSET + lease.slot * FS_DESC_BYTES),
                    &slot, sizeof(slot)) &&
        !ipc_call(work.state.call, &call, CALL_ASYNC, &result));
    rpc = result.request;
    if (attempt == 5) {
      CHECK(!ipc_call(work.state.call, &gate, CALL_CONTROL, &result));
      gate.words[1]++;
      CHECK(!refcall(&work, &cache, FS_CLOSE, keys[PIPE_WRITE]));
    }
    if (!attempt || attempt == 3) {
      CHECK(!ipc_call(work.state.call, &gate, CALL_CONTROL, &result));
      gate.words[1]++;
      if (!attempt)
        CHECK(!ipc_rqcancel(rpc));
      else
        page[FS_SLOT_BYTES - 1] = 'x';
      CHECK(!ipc_call(work.state.call, &gate, CALL_CONTROL, &result));
      gate.words[1]++;
    }
    CHECK(!ipc_rqwait(rpc, 0, &result) && (isize) result.words[0] == status[attempt] &&
          result.words[1] == lease.seq && result.words[2] == (attempt == 3 || attempt == 4) &&
          result.words[3] == (attempt == 4) && page[FS_SLOT_BYTES - 2] == 0xa5 &&
          page[FS_SLOT_BYTES - 1] == (attempt == 3   ? 'x'
                                      : attempt == 4 ? 'p'
                                                     : 0xa5));
    CHECK(!ipc_rqwait(rpc, RQ_FINISH, &result) && !ipc_rqack(rpc, ACK_RESULT | ACK_RELEASE) &&
          !sysconnworkrelease(&work, &cache, lease));
    pipeiotrace[0] = attempt + 1;
  }
  CHECK(!ipc_call(work.state.call, &gate, CALL_CONTROL, &result));
  gate.words[1]++;
  CHECK(!sysconnworkreserve(&work, &cache, SYSFILE_MAIN, 1, &lease));
  call.words[0] = FS_READ;
  call.words[2] = slot.seq = lease.seq;
  call.words[3] = lease.slot;
  slot.object = result.words[2];
  CHECK(!__sys_copy((void *) (work.state.map.address + FS_DESC_OFFSET + lease.slot * FS_DESC_BYTES),
                    &slot, sizeof(slot)) &&
        !ipc_call(work.state.call, &call, CALL_ASYNC, &result) &&
        !ipc_call(work.state.call, &gate, CALL_CONTROL, &result));
}

int
main(int argc, char **argv)
{
  const Bootview *boot = sysboot();
  CHECK(boot && argc == 1);
  if (argv[0][0] == 's')
    server(boot);
  else if (argv[0][0] == 'S')
    serviceloop(boot, 0);
  else if (argv[0][0] == 'I')
    serviceloop(boot, 1);
  else if (argv[0][0] == 'i')
    interruptclient();
  else if (argv[0][0] == 'C')
    serviceclient(boot);
  else
    run(boot);
  return 42;
}
