#include <errno.h>
#include <fcntl.h>
#include <poll.h>
#include <signal.h>
#include <termios.h>
#include <proto/tty.h>
#include <sys/boot.h>
#include <sys/channel.h>
#include <time.h>
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
#include "private/timer.h"
#include "../../sbin/fs/pipefile.h"
#include "../../sbin/fs/access.h"
#include "../../sbin/fs/mount.h"
#include "../../sbin/fs/openpath.h"
#include "../../sbin/fs/read.h"
#include "../../sbin/fs/seek.h"
#include "../../sbin/fs/serve.h"

#define ADDRESS     0x1600000UL
#define PIPETEST    0xf008
#define TEST_OFFSET INT64_C(0x100000003)

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
  Pipeready       ready;
  u8              byte = 'z';
  CHECK(!pipefilecreate(&table, pid, image, 1, keys) && keys[0].session != keys[1].session &&
        keys[0].reference != keys[1].reference &&
        pipefilepin(&table, pid, image + 1, keys[0].session, PIPE_READ, &pinned) == -ERR_STALE &&
        pipefilepin(&table, pid, image, keys[0].session, PIPE_WRITE, &pinned) == -ERR_DENIED &&
        !pipefileready(&table, pid, image, keys[PIPE_WRITE].session, &ready) &&
        ready.room == PIPE_CAPACITY && !ready.error && !ready.hup &&
        pipefileready(&table, pid, image + 1, keys[PIPE_WRITE].session, &ready) == -ERR_STALE &&
        ready.room == PIPE_CAPACITY && pipewrite(&files[0].stream, &byte, 1) == 1 &&
        !pipefileclose(&table, pid, image, keys[PIPE_WRITE]) &&
        !pipefileready(&table, pid, image, keys[PIPE_READ].session, &ready) &&
        ready.readable == 1 && ready.hup && !ready.error && !ready.room &&
        piperead(&files[0].stream, &byte, 1) == 1 &&
        !pipefileready(&table, pid, image, keys[PIPE_READ].session, &ready) && !ready.readable &&
        ready.hup);
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
        !pipefileclose(&table, pid, image, keys[PIPE_READ]) &&
        !pipefileready(&table, pid, image, keys[PIPE_WRITE].session, &ready) && ready.error &&
        !ready.room && !ready.hup && !ready.readable && !pipefileretire(&table, pid, image));
}

static void
server(const Bootview *boot)
{
  static u8       fill[PIPE_CAPACITY];
  static Pipefile files[1];
  static Pipewait waits[2];
  Piperef         refs[3] = {{0}};
  Pipetable       table = {files, refs, ARRAY_SIZE(files), ARRAY_SIZE(refs), 0};
  Pipedispatch    dispatch = {&table, waits, ARRAY_SIZE(waits), NULL, 0, NULL, NULL};
  Pipekey         keys[2];
  Pipe           *pipe = &files[0].stream;
  Filelink        link = {0};
  Ipcmsg          input;
  u32             attempt = 0, phase = 0, i;
  Vcred           real, effective, root = {0, 0};
  CHECK(!ipc_recv(0, 0, &input) && !vcredsource(&input, 1, &real) &&
        !vcredsource(&input, 0, &effective) && real.uid == 21 && real.gid == 31 &&
        effective.uid == 22 && effective.gid == 32 &&
        !vaccess(&effective, 22, 32, 0100, VACCESS_X, 1) &&
        vaccess(&effective, 22, 32, 0010, VACCESS_X, 1) == -EACCES &&
        !vaccess(&effective, 21, 32, 0010, VACCESS_X, 1) &&
        vaccess(&real, 21, 32, 0010, VACCESS_X, 1) == -EACCES &&
        !vaccess(&real, 22, 32, 0004, VACCESS_R, 0) &&
        vaccess(&root, 22, 32, 0, VACCESS_X, 0) == -EACCES &&
        !vaccess(&root, 22, 32, 0, VACCESS_X, 1));
  CHECK(!sysfilehello(&link, 1, bootresource(boot, 256), bootresource(boot, BOOT_SELF_ALLOC),
                      bootresource(boot, BOOT_SELF_SPACE), ADDRESS, &input));
  CHECK(vcredsource(&input, 0, &effective) < 0 && effective.uid == 22 && effective.gid == 32);
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
pollserverscript(const Bootview *boot, Fsservice *fs, Pipefile *file, int writer)
{
  Pipepoll *wait = fs->pipes.polls;
  Filelink *link = fs->links;
  Ipcmsg    request, gate, result;
  Msg       message = {{PIPETEST, 0, 0, 0}}, reply = {{0, 0, 0, 0}};
  u64       flags;
  u8        byte = 'r';
  u32       i;
  CHECK(!fsstep(fs) && link->ready && !fsstep(fs) && file->session[PIPE_READ]);
  for (i = 0; i < 2; i++) {
    CHECK(!ipc_recv(0, 0, &request) && request.words[0] == FS_POLL_QUERY &&
          !pipedispatch(&fs->pipes, link, 1, &request));
  }
  CHECK(!ipc_recv(0, 0, &request) && request.words[0] == FS_POLL_WAIT &&
        !pipedispatch(&fs->pipes, link, 1, &request) && wait->request == request.request);
  if (writer) {
    CHECK(!ipc_recv(0, 0, &gate) && gate.words[0] == PIPETEST && !ipc_reply(gate.request, &reply) &&
          pipewrite(&file->stream, &byte, 1) == 1 && !pipepump(&fs->pipes) && !wait->request);
  } else {
    Handle old = request.request;
    u64    slot = request.words[3];
    CHECK(!ipc_call(bootresource(boot, 257), &message, 0, &result));
    /* A fresh query must have its own slot while the cancelled WAIT still owns its page. */
    CHECK(!ipc_recv(0, 0, &request) && request.words[0] == FS_POLL_QUERY &&
          request.words[3] != slot && !ipc_rqcheck(old, &flags) &&
          flags == (RQ_CHECK_LIVE | RQ_CHECK_CANCEL) &&
          !pipedispatch(&fs->pipes, link, 1, &request) && !pipepump(&fs->pipes) && !wait->request);
  }
  if (writer)
    CHECK(!ipc_recv(0, 0, &request) && request.words[0] == FS_POLL_QUERY &&
          !pipedispatch(&fs->pipes, link, 1, &request));
  CHECK(!ipc_recv(0, 0, &request) && request.words[0] == FS_POLL_QUERY);
  if (writer) {
    CHECK(file->stream.bytes == 1 && !pipedispatch(&fs->pipes, link, 1, &request));
  } else {
    CHECK(!ipc_defer(request.request) && !ipc_recv(0, 0, &gate) && gate.source == request.source &&
          gate.words[0] == PIPETEST && !ipc_rqcheck(request.request, &flags) &&
          flags == (RQ_CHECK_LIVE | RQ_CHECK_CANCEL));
    reply.words[0] = (u64) -EINTR;
    reply.words[1] = request.words[2];
    CHECK(!ipc_reply(request.request, &reply));
    reply.words[0] = reply.words[1] = 0;
    CHECK(!ipc_reply(gate.request, &reply));
  }
}

static void
serviceloop(const Bootview *boot, u32 interrupt)
{
  static Filelink link;
  static Pipefile file;
  static Pipewait wait;
  static Pipepoll poll;
  static Piperef  refs[2];
  Pipetable       table = {&file, refs, 1, ARRAY_SIZE(refs), 0};
  Fsservice       fs = {bootresource(boot, 256),
                        bootresource(boot, BOOT_SELF_ALLOC),
                        bootresource(boot, BOOT_SELF_SPACE),
                        ADDRESS,
                        &link,
                        1,
                        {&table, &wait, 1, &poll, 1, NULL, NULL},
                        NULL,
                        0};
  if (interrupt >= 8) {
    pollserverscript(boot, &fs, &file, interrupt == 8);
  } else if (interrupt == 1) {
    Ipcmsg request, gate;
    Sigsel target = {SIGNAL_SELECT_PID, 0};
    Msg    reply = {{0, 0, 0, 0}};
    u32    i;
    CHECK(!fsstep(&fs) && link.ready && !ipc_recv(0, 0, &request) && request.kind == IPC_CALL &&
          request.words[0] == FS_PIPE && !ipc_defer(request.request));
    target.target = request.source;
    CHECK(!__sys_sigsend(&target, SIGNAL_USR1) && !ipc_recv(0, 0, &gate) && gate.kind == IPC_CALL &&
          gate.source == target.target && gate.words[0] == PIPETEST && !table.serial &&
          !ipc_reply(gate.request, &reply) && !pipedispatch(&fs.pipes, &link, 1, &request) &&
          table.serial == 4);
    CHECK(!fsstep(&fs) && !fsstep(&fs) && !fsstep(&fs) && table.serial == 8);
    for (i = 0; i < 2; i++)
      CHECK(!ipc_recv(0, 0, &request) && request.words[0] == FS_READ &&
            !pipedispatch(&fs.pipes, &link, 1, &request) && wait.io.request == request.request &&
            !__sys_sigsend(&target, SIGNAL_USR1) && !ipc_recv(0, 0, &gate) &&
            gate.source == target.target && gate.words[0] == PIPETEST &&
            !ipc_reply(gate.request, &reply) && !pipepump(&fs.pipes) && !wait.io.request);
    for (i = 0; i < 2; i++) {
      u64 flags;
      Msg cancelled = {{(u64) -EINTR, 0, 0, 0}};
      CHECK(!ipc_recv(0, 0, &request) && request.words[0] == FS_WRITE &&
            !ipc_defer(request.request) && !__sys_sigsend(&target, SIGNAL_USR1) &&
            !ipc_recv(0, 0, &gate) && gate.source == target.target && gate.words[0] == PIPETEST);
      cancelled.words[1] = request.words[2];
      CHECK(!ipc_rqcheck(request.request, &flags) && flags == (RQ_CHECK_LIVE | RQ_CHECK_CANCEL) &&
            !ipc_reply(gate.request, &reply) && !ipc_reply(request.request, &cancelled));
      do {
        CHECK(!fsstep(&fs));
      } while (!file.stream.bytes);
      do {
        CHECK(!fsstep(&fs));
      } while (file.stream.bytes);
    }
    for (i = 0; i < 2; i++) {
      Fileslot row;
      CHECK(!ipc_recv(0, 0, &request) && request.words[0] == FS_WRITE &&
            !sysfilerequest((const volatile void *) link.address, FS_CONN_BYTES, request.words[3],
                            request.words[2], &row) &&
            row.args[0] == TEST_OFFSET && row.args[3] == FS_IO_POSITIONAL && row.args[1] == 1);
      if (!i) {
        Msg cancelled = {{(u64) -EINTR, request.words[2], 0, 0}};
        u64 state;
        CHECK(!ipc_defer(request.request) && !__sys_sigsend(&target, SIGNAL_USR1) &&
              !ipc_recv(0, 0, &gate) && gate.source == target.target && gate.words[0] == PIPETEST &&
              !ipc_rqcheck(request.request, &state) && state == (RQ_CHECK_LIVE | RQ_CHECK_CANCEL) &&
              !ipc_reply(gate.request, &reply) && !ipc_reply(request.request, &cancelled));
      } else
        CHECK(!pipedispatch(&fs.pipes, &link, 1, &request) && !file.stream.bytes);
    }
    {
      Fileslot row;
      u64      state;
      CHECK(!ipc_recv(0, 0, &request) && request.words[0] == FS_SEEK &&
            !sysfilerequest((const volatile void *) link.address, FS_CONN_BYTES, request.words[3],
                            request.words[2], &row) &&
            (int64_t) row.args[0] == -3 && row.args[1] == FS_SEEK_CUR && !row.payload_bytes &&
            !ipc_defer(request.request) && !__sys_sigsend(&target, SIGNAL_USR1) &&
            !ipc_recv(0, 0, &gate) && gate.source == target.target && gate.words[0] == PIPETEST &&
            !ipc_rqcheck(request.request, &state) && state == (RQ_CHECK_LIVE | RQ_CHECK_CANCEL) &&
            !ipc_reply(gate.request, &reply));
      /* Model a File whose relative seek was committed before cancellation. */
      reply.words[1] = request.words[2];
      reply.words[2] = TEST_OFFSET;
      CHECK(!ipc_reply(request.request, &reply));
    }
    CHECK(!ipc_recv(0, 0, &request) && request.words[0] == FS_POLL_QUERY);
    CHECK(!pipedispatch(&fs.pipes, &link, 1, &request) && !poll.request);
    CHECK(!ipc_recv(0, 0, &request) && request.words[0] == FS_POLL_WAIT);
    CHECK(!pipedispatch(&fs.pipes, &link, 1, &request) && poll.request == request.request);
    CHECK(!__sys_sigsend(&target, SIGNAL_USR1));
    {
      u64 flags;
      CHECK(!ipc_recv(0, 0, &gate) && gate.source == target.target && gate.words[0] == PIPETEST &&
            !ipc_rqcheck(request.request, &flags) && flags == (RQ_CHECK_LIVE | RQ_CHECK_CANCEL));
    }
    CHECK(!ipc_reply(gate.request, &reply));
    while (poll.request) {
      CHECK(!pipepump(&fs.pipes));
      if (poll.request)
        CHECK(!ipc_recv(EVENT_RQ_CANCEL, RECV_EVENTS_ONLY, &gate) && gate.kind == IPC_EVENT);
    }
    CHECK(!ipc_recv(0, 0, &gate) && gate.source == target.target && gate.words[0] == PIPETEST &&
          !ipc_reply(gate.request, &reply));
    {
      Fileslot  row;
      u64       position = 0;
      const u64 expected[] = {17, INT64_MAX, INT64_MAX, INT64_MAX, INT64_MAX, 0};
      for (i = 0; i < ARRAY_SIZE(expected); i++) {
        CHECK(!ipc_recv(0, 0, &request) && request.words[0] == FS_SEEK &&
              !sysfilelinkaccept(&link, 1, &request, &row) &&
              row.object == file.session[PIPE_READ] &&
              !fileseek(&link.watch.active, &request, &row, 20, &position) &&
              position == expected[i] && !link.watch.active.requests[request.words[3]]);
      }
      {
        static Cacheblock blocks[2];
        u8                scratch[CACHE_BYTES] = {0};
        const Loadmap     map = {{7, 1, 1, CACHE_BYTES + 2}, {5}, 0};
        const u64         cursors[] = {2, 2, 3, CACHE_BYTES + 1, CACHE_BYTES + 2, CACHE_BYTES + 2};
        Cache             cache = {0};
        Readwork          readwork = {0};
        Readfetch         fetch;
        volatile u8      *page;
        CHECK(!cacheinit(&cache, blocks, ARRAY_SIZE(blocks), 16));
        for (i = 0; i < ARRAY_SIZE(cursors); i++) {
          CHECK(!ipc_recv(0, 0, &request) && !sysfilelinkaccept(&link, 1, &request, &row) &&
                row.object == file.session[PIPE_READ]);
          page = (volatile u8 *) (link.address + FS_DATA_OFFSET + request.words[3] * FS_SLOT_BYTES);
          if (!i) {
            page[0] = 0xa5;
            Cacheio stale;
            CHECK(filedata(&readwork, &fetch, &link.watch.active, &request, &row, &map, NULL,
                           &cache, 4, &position, scratch, page, NULL) == 1 &&
                  fetch.block == 5 &&
                  filedata(&readwork, &fetch, &link.watch.active, &request, &row, &map, NULL,
                           &cache, 4, &position, scratch, page, NULL) == -ERR_WOULD_BLOCK &&
                  !position && page[0] == 0xa5);
            stale = fetch.ticket;
            stale.serial++;
            scratch[0] = 'A';
            scratch[1] = 'B';
            scratch[2] = 'C';
            scratch[CACHE_BYTES - 1] = 'Z';
            CHECK(filereadcomplete(&readwork, &stale, scratch, sizeof(scratch)) == -ERR_STALE &&
                  !filereadcomplete(&readwork, &fetch.ticket, scratch, sizeof(scratch)));
          }
          if (i == 3)
            CHECK(!fileseek(&link.watch.active, &request, &row, map.file.length, &position));
          else
            CHECK(!filedata(&readwork, &fetch, &link.watch.active, &request, &row, &map, NULL,
                            &cache, 4, &position, scratch, page, NULL));
          CHECK(position == cursors[i] && !link.watch.active.requests[request.words[3]] &&
                !readwork.request && !readwork.span.cache);
        }
      }
      for (i = 0; i < 5; i++) {
        const Termarg mode = {
            TTY_ICRNL, TTY_OPOST | TTY_ONLCR, TTY_CS8, TTY_ICANON | TTY_ECHO, 115200, 115200, {3}};
        const u8    *bytes = (const u8 *) &mode;
        volatile u8 *page;
        Msg          attrs = {{0, 0, 0, sizeof(mode)}};
        u32          j;
        CHECK(!ipc_recv(0, 0, &request) && request.words[0] == FS_TTYCTL &&
              !sysfilelinkaccept(&link, 1, &request, &row) &&
              row.object == file.session[PIPE_READ] && row.args[0] == TTY_GETATTR && !row.args[1] &&
              !row.args[2] && !row.args[3] && row.args[4] == sizeof(mode) &&
              row.payload_bytes == sizeof(mode));
        attrs.words[1] = row.seq;
        if (i == 3) {
          u64 state;
          CHECK(!ipc_defer(request.request) && !__sys_sigsend(&target, SIGNAL_USR1));
          CHECK(!ipc_recv(0, 0, &gate) && gate.source == target.target &&
                gate.words[0] == PIPETEST && !ipc_rqcheck(request.request, &state) &&
                state == (RQ_CHECK_LIVE | RQ_CHECK_CANCEL) && !ipc_reply(gate.request, &reply));
          attrs.words[0] = (u64) -EINTR;
          attrs.words[3] = 0;
        } else {
          page = (volatile u8 *) (link.address + FS_DATA_OFFSET + request.words[3] * FS_SLOT_BYTES);
          for (j = 0; j < sizeof(mode); j++)
            page[j] = bytes[j];
          if (!i)
            attrs.words[3]--;
        }
        CHECK(!ipc_reply(request.request, &attrs) &&
              !sysfilefinish(&link.watch.active, request.request));
      }
    }
  } else if (interrupt == 6) {
    Ipcmsg request, gate;
    Msg    reply = {{(u64) -EINTR, 0, 0, 0}}, done = {{0, 0, 0, 0}};
    u64    flags;
    CHECK(!fsstep(&fs) && link.ready && !fsstep(&fs) && table.serial == 4);
    CHECK(!ipc_recv(0, 0, &request) && request.words[0] == FS_POLL_QUERY &&
          !ipc_defer(request.request));
    CHECK(!ipc_recv(0, 0, &gate) && gate.kind == IPC_CALL && gate.source == request.source &&
          gate.words[0] == PIPETEST && !ipc_rqcheck(request.request, &flags) &&
          flags == (RQ_CHECK_LIVE | RQ_CHECK_CANCEL));
    reply.words[1] = request.words[2];
    CHECK(!ipc_reply(request.request, &reply) && !ipc_reply(gate.request, &done));
    CHECK(!ipc_recv(0, 0, &request) && request.words[0] == FS_POLL_QUERY &&
          !pipedispatch(&fs.pipes, &link, 1, &request));
    CHECK(!ipc_recv(0, 0, &request) && request.words[0] == FS_POLL_QUERY &&
          !ipc_defer(request.request));
    pipeiotrace[0] = 7;
    return;
  } else if (interrupt == 7) {
    CHECK(!fsstep(&fs) && link.ready && !fsstep(&fs) && table.serial == 4);
    pipeiotrace[0] = 7;
    return;
  } else if (interrupt == 5) {
    CHECK(!fsstep(&fs) && link.ready && !fsstep(&fs) && !fsstep(&fs) && wait.io.request &&
          wait.io.end == PIPE_READ && !file.stream.bytes);
    CHECK(!fsstep(&fs) && wait.io.request && !file.stream.bytes);
    CHECK(!fsstep(&fs) && file.stream.bytes == 1 && !pipepump(&fs.pipes) && !wait.io.request &&
          !file.stream.bytes);
  } else if (interrupt) {
    Ipcmsg request, gate;
    Sigsel target = {SIGNAL_SELECT_PID, 0};
    Msg    reply = {{0, 0, 0, 0}};
    CHECK(!fsstep(&fs) && link.ready && !fsstep(&fs) && table.serial == 4 &&
          !ipc_recv(0, 0, &request) && request.kind == IPC_CALL && request.words[0] == FS_CLOSE &&
          !ipc_defer(request.request));
    target.target = request.source;
    CHECK((interrupt == 2 || !__sys_sigsend(&target, SIGNAL_USR1)) && !ipc_recv(0, 0, &gate) &&
          gate.kind == IPC_CALL && gate.source == target.target && gate.words[0] == PIPETEST &&
          !ipc_reply(gate.request, &reply) && !pipedispatch(&fs.pipes, &link, 1, &request));
  }
  do {
    CHECK(!fsstep(&fs));
  } while (!table.serial || link.ready);
  CHECK(!link.memory && !link.address && !link.watch.owner.cookie && !wait.io.request &&
        !poll.request && !file.stream.refs[0] && !file.stream.refs[1]);
  pipeiotrace[0] = 7;
}

static volatile int caught;
static Fileimage   *interruptedimage;
static Connwork    *interruptedwork;
static volatile int release_read;

static void
oninterrupt(int signal)
{
  Fileimage *image;
  Connwork  *work;
  Fdview     readend, writeend;
  if (signal == SIGUSR1) {
    if (caught) {
      caught++;
      if (release_read) {
        Msg    gate = {{PIPETEST, 0, 0, 0}};
        Ipcmsg reply;
        CHECK(!ipc_call(interruptedwork->state.call, &gate, CALL_CONTROL, &reply));
      }
      return;
    }
    CHECK(!sysfileimage(&image) && image == interruptedimage && !sysfileimagefs(1, 0, &work) &&
          work == interruptedwork && !sysfdobserve(&image->fds, 0, &readend) &&
          readend.tag.state == SYSFD_RESERVED && !sysfdobserve(&image->fds, 1, &writeend) &&
          writeend.tag.state == SYSFD_RESERVED && dup(0) == -1 && errno == EBADF);
    caught++;
  }
}

static void
transfer(Fileimage *image, Connwork *work, int fd, u64 opcode, u8 *bytes, u64 count, u64 expected,
         isize native)
{
  Conncache   *cache = sysfileimagecache(image);
  Connwork    *peer = NULL;
  Opkey        key;
  Fileioresult completed;
  u64          mask, ignored;
  CHECK(!__sys_sigmask(SIGNAL_BLOCK, SIGNAL_SUPPORTED, &mask));
  CHECK(!sysfileiostage(image, 0, fd, opcode, bytes, count, &peer, &key) && peer == work);
  if (opcode == FS_WRITE)
    bytes[0] = 0;
  CHECK(!sysfileiowait(image, 0, &key, mask, NULL));
  CHECK(!sysfileiofinish(image, &key, bytes, count, &completed) && completed.native == native &&
        !completed.business && completed.count == expected &&
        sysopcheck(work, cache, &key) == -ERR_STALE &&
        !__sys_sigmask(SIGNAL_SETMASK, mask, &ignored));
}

static void
seekpipes(Fileimage *image, Connwork *work, int fd)
{
  Conncache  *cache = sysfileimagecache(image);
  Fdview      view;
  Fileref     ref;
  Filelease   lease;
  Ipcmsg      result;
  Fileslot    slot = {0};
  Msg         request = {{FS_SEEK, work->state.map.connid, 0, 0}};
  u32         i;
  const isize expected[] = {-ESPIPE, -EINVAL, -EINVAL, -ESTALE};
  CHECK(!sysfdobserve(&image->fds, fd, &view) &&
        !sysfilerefview(&image->files, work->state.self, view.file, &ref));
  slot.state = FS_SLOT_SUBMITTED;
  slot.span = 1;
  slot.args[0] = (u64) -1;
  for (i = 0; i < ARRAY_SIZE(expected); i++) {
    CHECK(!sysconnworkreserve(work, cache, SYSFILE_MAIN, 1, &lease));
    request.words[2] = slot.seq = lease.seq;
    request.words[3] = lease.slot;
    slot.object = i == 3 ? UINT64_MAX : ref.session;
    slot.args[1] = i == 1 ? FS_SEEK_END + 1 : FS_SEEK_CUR;
    slot.payload_bytes = i == 2 ? 1 : 0;
    CHECK(!__sys_copy(
              (void *) (work->state.map.address + FS_DESC_OFFSET + lease.slot * FS_DESC_BYTES),
              &slot, sizeof(slot)) &&
          !ipc_call(work->state.call, &request, 0, &result) &&
          (isize) result.words[0] == expected[i] && result.words[1] == lease.seq &&
          !result.words[2] && !result.words[3] && !sysconnworkrelease(work, cache, lease));
  }
}

static void
pollsnapshot(Fileimage *image, Connwork *work, const int fds[2])
{
  Pollfd   input[] = {{fds[0], FS_POLL_IN, 0},
                      {-1, FS_POLL_OUT, 0},
                      {BOOT_FD_COUNT, FS_POLL_IN, 0},
                      {2, FS_POLL_OUT | FS_POLL_HUP, 0},
                      {fds[0], FS_POLL_PRI, 0}};
  Pollfile items[ARRAY_SIZE(input)];
  Filepoll rows[3];
  u32      indices[ARRAY_SIZE(rows)];
  u64      mask, ignored, file;
  Conntag  stale = work->state.tag;
  Fileref  ref;
  CHECK(dup(fds[0]) == 2 && !__sys_sigmask(SIGNAL_BLOCK, SIGNAL_SUPPORTED, &mask) &&
        !sysfilepollcapture(image, input, items, ARRAY_SIZE(items)) && !items[1].row.revents &&
        items[2].row.revents == FS_POLL_NVAL && items[3].row.events == FS_POLL_OUT &&
        items[0].file == items[3].file && items[0].file == items[4].file &&
        !sysfilerefview(&image->files, image->files.self, items[0].file, &ref) && ref.pins == 3 &&
        sysfilepollgroup(items, ARRAY_SIZE(items), stale, rows, indices, ARRAY_SIZE(rows)) == 3 &&
        indices[0] == 0 && indices[1] == 3 && indices[2] == 4 &&
        rows[0].session == rows[1].session && rows[0].session == rows[2].session &&
        sysfilepollcurrent(image, &items[3]) == 1);
  stale.generation++;
  file = items[0].file;
  CHECK(!sysfilepollgroup(items, ARRAY_SIZE(items), stale, rows, indices, ARRAY_SIZE(rows)) &&
        dup2(fds[1], 2) == 2 && !sysfilepollcurrent(image, &items[3]) &&
        sysfilepollcurrent(image, &items[0]) == 1 &&
        !sysfilerefview(&image->files, image->files.self, items[0].file, &ref) && ref.pins == 3 &&
        !sysfilepollrelease(image, items, ARRAY_SIZE(items)) &&
        !sysfilepollrelease(image, items, ARRAY_SIZE(items)) &&
        !sysfilerefview(&image->files, image->files.self, file, &ref) && !ref.pins);
  CHECK(!__sys_sigmask(SIGNAL_SETMASK, mask, &ignored) && !close(2));
}

static void
interruptclient(void)
{
  Fileimage            *image;
  Connwork             *work = NULL;
  Conncache            *cache;
  struct sigaction      action = {0};
  const struct timespec pause = {0, 30000000};
  Pipecreate            pipe = {0};
  Rkey                  active = {0};
  Msg                   gate = {{PIPETEST, 0, 0, 0}};
  Ipcmsg                result;
  Handle                rpc;
  isize                 business;
  int                   fds[2] = {-1, -1};
  int                   fd;
  Fdview                original, alias, displaced;
  Fileref               ref;
  Connwork             *draftpeer = NULL;
  Opkey                 draft = {0};
  Fileioresult          completed;
  u64                   mask, ignored;
  u64                   originalseq, originalfile;
  u32                   slot, restart = 2;
  u8                    data[3] = {'p', 'q', 0xa5};
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
  CHECK(sysfileimagepipe(0, work, &pipe, &active, fds, &business) == -ERR_INTERRUPTED &&
        caught == 1 && errno == EBUSY && !active.sequence &&
        !sysoprpc(work, cache, &pipe.operation, &rpc) && rpc &&
        !sysfdcheckpair(&image->fds, pipe.tags) && fds[0] == -1 && fds[1] == -1 &&
        !ipc_call(work->state.call, &gate, CALL_CONTROL, &result));
  CHECK(sysfileimagepipe(0, work, &pipe, &active, (int *) 1, &business) == -ERR_FAULT &&
        !pipe.operation.lease.seq && !business && !sysfdobserve(&image->fds, 0, &original) &&
        original.tag.state == SYSFD_FREE && !sysfdobserve(&image->fds, 1, &alias) &&
        alias.tag.state == SYSFD_FREE && !nanosleep(&pause, NULL) &&
        !sysfilerefconnection(&image->files, work->state.self, work->state.tag));
  CHECK(!sysfileimagepipe(0, work, &pipe, &active, fds, &business) && !pipe.operation.lease.seq &&
        !active.sequence && !business && fds[0] == 0 && fds[1] == 1 && caught == 1);
  pollsnapshot(image, work, fds);
  CHECK(!__sys_sigmask(SIGNAL_BLOCK, SIGNAL_SUPPORTED, &mask) &&
        sysfileiostage(image, 0, 2, FS_WRITE, data, 1, &draftpeer, &draft) == -ERR_BAD_HANDLE &&
        sysfileiostage(image, 0, fds[1], FS_WRITE, (const void *) 1, 1, &draftpeer, &draft) ==
            -ERR_FAULT &&
        !draftpeer && !draft.lease.seq && !sysfdobserve(&image->fds, fds[1], &original) &&
        !sysfilerefview(&image->files, work->state.self, original.file, &ref) && !ref.pins &&
        !__sys_sigmask(SIGNAL_SETMASK, mask, &ignored));
  release_read = 1;
  CHECK(read(fds[0], data, 1) == -1 && errno == EINTR && caught == 2 && data[0] == 'p');
  release_read = 0;
  errno = EBUSY;
  action.sa_flags = SA_RESTART;
  CHECK(!sigaction(SIGUSR1, &action, NULL) &&
        !__sys_sigmask(SIGNAL_BLOCK, SIGNAL_SUPPORTED, &mask) &&
        !sysfileiostage(image, 0, fds[0], FS_READ, NULL, 1, &draftpeer, &draft) &&
        sysfileiowait(image, 0, &draft, mask, &restart) == -ERR_INTERRUPTED && restart == 1 &&
        caught == 3 && sysfileioresume(image, &draft) == -ERR_WOULD_BLOCK &&
        !sysoprpc(work, cache, &draft, &rpc) &&
        sysfileiowait(image, 0, &draft, mask, &restart) == -ERR_TIMEDOUT);
  slot = draft.lease.slot;
  CHECK(sysfileioresume(image, &draft) == SYSOP_ABANDONED && !draft.lease.seq &&
        work->state.operations[slot].rpc == rpc && !work->state.operations[slot].owner &&
        !sysfdobserve(&image->fds, fds[0], &original) &&
        !sysfilerefview(&image->files, work->state.self, original.file, &ref) && ref.pins == 1 &&
        !__sys_sigmask(SIGNAL_SETMASK, mask, &ignored) &&
        !ipc_call(work->state.call, &gate, CALL_CONTROL, &result) && !nanosleep(&pause, NULL) &&
        !work->state.operations[slot].phase && ipc_rqpoll(rpc, 0, &result) < 0 &&
        !sysfilerefview(&image->files, work->state.self, original.file, &ref) && !ref.pins);
  CHECK(!__sys_sigmask(SIGNAL_BLOCK, SIGNAL_SUPPORTED, &mask) &&
        !sysfileiostage(image, 0, fds[1], FS_WRITE, data, 1, &draftpeer, &draft) &&
        sysfileiowait(image, 0, &draft, mask, &restart) == -ERR_INTERRUPTED && restart == 1 &&
        caught == 4 && sysfileiorestart(image, &draft, 1) == -ERR_WOULD_BLOCK);
  originalseq = draft.lease.seq;
  originalfile = work->state.operations[draft.lease.slot].file;
  data[0] = 'x';
  *(volatile u8 *) (work->state.map.address + FS_DATA_OFFSET + draft.lease.slot * FS_SLOT_BYTES) =
      'y';
  CHECK(dup2(fds[1], 2) == 2);
  CHECK(dup2(fds[0], fds[1]) == fds[1] &&
        !ipc_call(work->state.call, &gate, CALL_CONTROL, &result) &&
        !sysfileiowait(image, 0, &draft, mask, &restart) && !sysfileiorestart(image, &draft, 1) &&
        draft.lease.seq > originalseq &&
        work->state.operations[draft.lease.slot].file == originalfile &&
        !sysfileiowait(image, 0, &draft, mask, &restart) &&
        sysfileiorestart(image, &draft, 1) == -ERR_STATE &&
        !sysfileiofinish(image, &draft, NULL, 1, &completed) && !completed.native &&
        !completed.business && completed.count == 1 && dup2(2, fds[1]) == fds[1] && !close(2) &&
        !__sys_sigmask(SIGNAL_SETMASK, mask, &ignored));
  transfer(image, work, fds[0], FS_READ, data, 1, 1, 0);
  CHECK(data[0] == 'p');
  release_read = 1;
  CHECK(write(fds[1], "pq", 2) == 2 && read(fds[0], data, sizeof(data)) == 2 && data[0] == 'p' &&
        data[1] == 'q' && data[2] == 0xa5 && errno == EBUSY && caught == 5);
  CHECK(pwrite(fds[1], "z", 1, TEST_OFFSET) == -1 && errno == ESPIPE && caught == 6);
  CHECK(lseek(fds[0], -3, SEEK_CUR) == TEST_OFFSET && caught == 7);
  {
    struct pollfd row;
    row.fd = fds[0];
    row.events = POLLIN;
    row.revents = POLLPRI;
    CHECK(poll(&row, 1, -1) == -1 && errno == EINTR && caught == 8 && row.revents == POLLPRI);
    CHECK(!ipc_call(work->state.call, &gate, CALL_CONTROL, &result));
  }
  release_read = 0;
  fd = dup(fds[0]);
  CHECK(fd == 2 && lseek(fds[0], -3, SEEK_END) == 17 &&
        lseek(fd, INT64_MAX, SEEK_SET) == INT64_MAX && lseek(fds[0], 1, SEEK_CUR) == -1 &&
        errno == EOVERFLOW && lseek(fd, INT64_MIN, SEEK_CUR) == -1 && errno == EINVAL &&
        lseek(fds[0], 0, SEEK_CUR) == INT64_MAX && lseek(fd, -INT64_MAX, SEEK_CUR) == 0);
  CHECK(read(fd, data, 2) == 2 && data[0] == 'A' && data[1] == 'B' &&
        pread(fds[0], data, sizeof(data), CACHE_BYTES - 1) == 3 && data[0] == 'Z' && !data[1] &&
        !data[2] && read(fds[0], data, 1) == 1 && data[0] == 'C' &&
        lseek(fd, -1, SEEK_END) == CACHE_BYTES + 1 && read(fd, data, sizeof(data)) == 1 &&
        !data[0] && !read(fds[0], data, sizeof(data)));
  {
    struct termios attrs = {0};
    attrs.c_iflag = UINT64_MAX;
    CHECK(tcgetattr(fd, &attrs) == -1 && errno == EIO && attrs.c_iflag == UINT64_MAX &&
          !attrs.c_ospeed && tcgetattr(fd, (void *) 1) == -1 && errno == EFAULT &&
          !tcgetattr(fd, &attrs) && errno == EFAULT && attrs.c_iflag == ICRNL &&
          attrs.c_oflag == (OPOST | ONLCR) && attrs.c_cflag == CS8 &&
          attrs.c_lflag == (ICANON | ECHO) && attrs.c_ispeed == 115200 &&
          attrs.c_ospeed == 115200 && attrs.c_cc[VINTR] == 3 && !attrs.c_cc[VQUIT] &&
          tcgetattr(-1, &attrs) == -1 && errno == EBADF);
    action.sa_flags = 0;
    release_read = 1;
    CHECK(!sigaction(SIGUSR1, &action, NULL) && !isatty(fd) && errno == EINTR && caught == 9);
    release_read = 0;
    errno = EBUSY;
    CHECK(isatty(fd) == 1 && errno == EBUSY && !isatty(fds[0]) && errno == ENOTTY &&
          tcgetattr(fds[0], &attrs) == -1 && errno == ENOTTY && attrs.c_ispeed == 115200 &&
          !close(fd) && !isatty(fd) && errno == EBADF);
  }
  data[0] = 'p';
  data[1] = 'q';
  data[2] = 0xa5;
  transfer(image, work, fds[1], FS_WRITE, data, 1, 1, 0);
  transfer(image, work, fds[0], FS_READ, (u8 *) 1, 1, 0, -ERR_FAULT);
  data[0] = 'p';
  transfer(image, work, fds[1], FS_WRITE, data, 2, 2, 0);
  CHECK(!data[0]);
  transfer(image, work, fds[0], FS_READ, data, sizeof(data), 2, 0);
  CHECK(data[0] == 'p' && data[1] == 'q' && data[2] == 0xa5);
  CHECK(write(fds[1], "p", 1) == 1 && pread(fds[0], data, 1, -1) == -1 && errno == EINVAL &&
        pread(fds[0], data + 2, 1, TEST_OFFSET) == -1 && errno == ESPIPE && data[2] == 0xa5);
  seekpipes(image, work, fds[0]);
  seekpipes(image, work, fds[1]);
  CHECK(lseek(fds[0], 0, SEEK_SET) == -1 && errno == ESPIPE && lseek(fds[1], 0, SEEK_END) == -1 &&
        errno == ESPIPE && lseek(fds[0], 0, -1) == -1 && errno == EINVAL &&
        lseek(-1, 0, SEEK_SET) == -1 && errno == EBADF);
  CHECK(read(fds[0], data, 1) == 1 && data[0] == 'p');
  CHECK(!sysfdobserve(&image->fds, fds[0], &original) && !fcntl(fds[0], F_SETFD, FD_CLOEXEC) &&
        fcntl(fds[0], F_GETFD) == FD_CLOEXEC && fcntl(fds[0], F_DUPFD, 4) == 4 &&
        fcntl(4, F_DUPFD, 4) == 5 && !fcntl(4, F_GETFD) && !fcntl(4, F_SETFD, FD_CLOEXEC) &&
        fcntl(4, F_GETFD) == FD_CLOEXEC && !fcntl(5, F_GETFD) && !fcntl(4, F_SETFD, 0) &&
        !fcntl(4, F_GETFD) && fcntl(fds[0], F_GETFD) == FD_CLOEXEC && !close(4) && !close(5) &&
        fcntl(4, F_GETFD) == -1 && errno == EBADF && fcntl(4, F_SETFD, 0) == -1 && errno == EBADF &&
        fcntl(fds[0], F_DUPFD, -1) == -1 && errno == EINVAL &&
        fcntl(fds[0], F_DUPFD, BOOT_FD_COUNT) == -1 && errno == EINVAL);
  errno = EBUSY;
  for (fd = 2; fd < BOOT_FD_COUNT; fd++)
    CHECK(dup(fds[0]) == fd && errno == EBUSY && !sysfdobserve(&image->fds, fd, &alias) &&
          alias.file == original.file && !alias.flags);
  CHECK(fcntl(fds[0], F_DUPFD, 4) == -1 && errno == EMFILE && dup(fds[0]) == -1 &&
        errno == EMFILE && dup(-1) == -1 && errno == EBADF &&
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
  CHECK(!ipc_rqwait(work->state.operations[6].rpc, RQ_FINISH, &result) &&
        dup2(fds[0], fds[0]) == fds[0] && !work->state.operations[6].rpc &&
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

/* These fixture roles run in different images. Share their storage so the
   trusted loader does not need separate page handles for both workspaces. */
static Connwork  work;
static Conncache cache;
static Fdtable   table;
static Filepool  pool;
static Rbank     bank;

static void
querypipes(const int fds[2], u32 hangup)
{
  Filepoll           rows[2];
  volatile Filepoll *page;
  Fdview             view;
  Fileref            ref;
  Fileioresult       result;
  u64                anchor, pin, mask, ignored;
  u32                i;
  for (i = 0; i < ARRAY_SIZE(rows); i++) {
    CHECK(!sysfdobserve(&table, fds[hangup ? 0 : i], &view) &&
          !sysfilerefview(&pool, work.state.self, view.file, &ref));
    rows[i].session = ref.session;
    rows[i].fd_generation = view.tag.generation;
    rows[i].events = hangup ? 0 : FS_POLL_IN | FS_POLL_OUT;
    rows[i].revents = UINT64_MAX;
  }
  anchor = view.file;
  CHECK(!__sys_sigmask(SIGNAL_BLOCK, SIGNAL_SUPPORTED, &mask));
  for (i = 0; i < (hangup ? 1U : 3U); i++) {
    Handle    rpc;
    Ipcmsg    finish;
    Opkey     key;
    Filelease lease;
    u64       session = rows[1].session;
    pin = anchor;
    rows[0].revents = rows[1].revents = UINT64_MAX;
    if (i == 1)
      rows[1].session = UINT64_MAX;
    CHECK(!sysfilerefpin(&pool, pool.self, pin) &&
          !sysoppollprepare(&work, &cache, SYSFILE_MAIN, 130 + i, &pin,
                            hangup ? FS_POLL_WAIT : FS_POLL_QUERY, 0, rows, ARRAY_SIZE(rows),
                            &key) &&
          !pin && !sysfilepollsubmit(&work, &cache, &key) && !sysoprpc(&work, &cache, &key, &rpc) &&
          !ipc_rqwait(rpc, RQ_FINISH, &finish));
    lease = key.lease;
    page = (volatile Filepoll *) (work.state.map.address + FS_DATA_OFFSET +
                                  lease.slot * FS_SLOT_BYTES);
    if (i == 2)
      page[1].revents = FS_POLL_NVAL;
    CHECK(!sysfilepollcollect(&work, &cache, &key, rows, ARRAY_SIZE(rows), &result, &pin) &&
          !key.lease.seq && pin == anchor && !work.state.pages.pages[lease.slot].seq &&
          !sysfilerefunpin(&pool, pool.self, pin) && ipc_rqpoll(rpc, 0, &finish) == -ERR_STALE);
    if (!i)
      CHECK(!result.native && !result.business && result.count == (hangup ? 2U : 1U) &&
            rows[0].revents == (hangup ? FS_POLL_HUP : 0) &&
            rows[1].revents == (hangup ? FS_POLL_HUP : FS_POLL_OUT));
    else
      CHECK(result.native == (i == 2 ? -ERR_STATE : 0) &&
            result.business == (i == 1 ? -ESTALE : 0) && !result.count &&
            rows[0].revents == UINT64_MAX && rows[1].revents == UINT64_MAX);
    rows[1].session = session;
  }
  CHECK(!__sys_sigmask(SIGNAL_SETMASK, mask, &ignored));
}

static volatile void *
submitrow(u64 opcode, Fileslot *slot, Filelease *lease, const void *input, u64 options,
          Ipcmsg *result)
{
  Msg          call = {{opcode, work.state.map.connid, 0, 0}};
  volatile u8 *page;
  CHECK(!sysconnworkreserve(&work, &cache, SYSFILE_MAIN, 1, lease));
  call.words[2] = slot->seq = lease->seq;
  call.words[3] = lease->slot;
  page = (volatile u8 *) (work.state.map.address + FS_DATA_OFFSET + lease->slot * FS_SLOT_BYTES);
  CHECK(
      !__sys_copy((void *) page, input, slot->payload_bytes) &&
      !__sys_copy((void *) (work.state.map.address + FS_DESC_OFFSET + lease->slot * FS_DESC_BYTES),
                  slot, sizeof(*slot)) &&
      !ipc_call(work.state.call, &call, options, result));
  return page;
}

static void
waitpipes(const int fds[2])
{
  Fileslot           slot = {0}, write = {0};
  Filelease          lease, other;
  Filepoll           row;
  volatile Filepoll *page;
  Ipcmsg             result;
  Fileref            ref;
  Fdview             view;
  Handle             rpc;
  Opkey              key, background;
  u64                anchor, pin, mask, ignored;
  u32                i;
  CHECK(!sysfdobserve(&table, fds[0], &view) &&
        !sysfilerefview(&pool, work.state.self, view.file, &ref));
  row.session = ref.session;
  row.fd_generation = view.tag.generation;
  row.events = FS_POLL_IN;
  row.revents = UINT64_MAX;
  anchor = view.file;
  slot.span = 1;
  slot.state = FS_SLOT_SUBMITTED;
  slot.payload_bytes = sizeof(row);
  slot.args[0] = slot.args[2] = 1;
  CHECK(!sysfdobserve(&table, fds[1], &view) &&
        !sysfilerefview(&pool, work.state.self, view.file, &ref));
  write.object = ref.session;
  write.span = write.payload_bytes = write.args[1] = 1;
  write.state = FS_SLOT_SUBMITTED;
  CHECK(!__sys_sigmask(SIGNAL_BLOCK, SIGNAL_SUPPORTED, &mask));
  for (i = 0; i < 2; i++) {
    if (!i) {
      pin = anchor;
      CHECK(!sysfilerefpin(&pool, pool.self, pin) &&
            !sysoppollprepare(&work, &cache, SYSFILE_MAIN, 120, &pin, FS_POLL_WAIT, 0, &row, 1,
                              &key) &&
            !pin && !sysfilepollsubmit(&work, &cache, &key) &&
            !sysoprpc(&work, &cache, &key, &rpc));
      lease = key.lease;
      page = (volatile Filepoll *) (work.state.map.address + FS_DATA_OFFSET +
                                    lease.slot * FS_SLOT_BYTES);
    } else {
      page = submitrow(FS_POLL_WAIT, &slot, &lease, &row, CALL_ASYNC, &result);
      rpc = result.request;
    }
    (void) submitrow(FS_POLL_WAIT, &slot, &other, &row, 0, &result);
    CHECK((isize) result.words[0] == -EAGAIN && result.words[1] == other.seq && !result.words[2] &&
          !result.words[3] && !sysconnworkrelease(&work, &cache, other) &&
          ipc_rqpoll(rpc, 0, &result) == -ERR_WOULD_BLOCK && page->revents == (i ? UINT64_MAX : 0));
    if (!i) {
      Fileioresult pending;
      CHECK(sysfilepollcollect(&work, &cache, &key, &row, 1, &pending, &pin) == -ERR_WOULD_BLOCK &&
            !pin && row.revents == UINT64_MAX &&
            !sysoppollabandon(&work, &cache, &key, &background) && !key.lease.seq &&
            work.state.pages.pages[lease.slot].seq == lease.seq);
    } else {
      /* Only the admitted snapshot determines readiness after a defer. */
      page->session = UINT64_MAX;
      page->events = 0;
      (void) submitrow(FS_WRITE, &write, &other, "z", 0, &result);
      CHECK(!result.words[0] && result.words[2] == 1 && !sysconnworkrelease(&work, &cache, other));
    }
    CHECK(!ipc_rqwait(rpc, 0, &result) && result.words[1] == lease.seq);
    if (!i)
      CHECK((isize) result.words[0] == -EINTR && !result.words[2] && !result.words[3] &&
            !page->revents && !ipc_rqwait(rpc, RQ_FINISH, &result) &&
            !sysopdrain(&work, &cache, &background, &pin) && pin == anchor &&
            !sysfilerefunpin(&pool, pool.self, pin) && !work.state.pages.pages[lease.slot].seq);
    else
      CHECK(!result.words[0] && result.words[2] == 1 && result.words[3] == sizeof(row) &&
            page->revents == FS_POLL_IN && page->session == UINT64_MAX && !page->events &&
            !ipc_rqwait(rpc, RQ_FINISH, &result) && !ipc_rqack(rpc, ACK_RESULT | ACK_RELEASE) &&
            !sysconnworkrelease(&work, &cache, lease));
  }
  CHECK(!__sys_sigmask(SIGNAL_SETMASK, mask, &ignored));
}

static void
serviceclient(const Bootview *boot)
{
  Fdtag      held;
  Pipecreate pipe = {0};
  Fdview     view;
  u64        closing;
  isize      business;
  int        fd[2] = {-1, -1};
  Handle     call = bootresource(boot, BOOT_FS_CALL);
  Handle     rpc;
  Fileclose  anchor = {0}, retry = {0};
  u64        deadline, mask, ignored;
  Opkey      foreground = {0}, background = {0};
  Fileref    reference;
  Msg        invalid = {{FS_HELLO, 0, 23, 0}};
  Ipcmsg     result;
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
  querypipes(fd, 0);
  waitpipes(fd);
  submitpipe(&bank, &work, &cache, &pool, &table, &pipe, 104);
  CHECK(!sysfilepipefinish(&pool, &work, &cache, &table, &pipe, fd, &business) &&
        business == -ENFILE && fd[0] == 0 && fd[1] == 2 && !pipe.operation.lease.seq &&
        !sysfdobserve(&table, 3, &view) && view.tag.state == SYSFD_FREE &&
        !sysfdobserve(&table, fd[0], &view) && view.tag.state == SYSFD_LIVE && !view.flags &&
        !sysfdobserve(&table, fd[1], &view) && view.tag.state == SYSFD_LIVE && !view.flags &&
        !sysfdclose(&table, &pool, work.state.self, view.tag, &closing) && closing == view.file);
  CHECK(!systimernow(&deadline) && !__sys_sigmask(SIGNAL_BLOCK, SIGNAL_SUPPORTED, &mask));
  deadline += UINT64_C(1000000000);
  CHECK(!sysfileclosesubmit(&pool, &work, &cache, closing, deadline, &anchor, &foreground) &&
        anchor.file == closing && foreground.lease.slot == 6 &&
        !sysoprpc(&work, &cache, &foreground, &rpc) &&
        sysfileclosesubmit(&pool, &work, &cache, closing, deadline, &retry, &background) ==
            -ERR_BUSY &&
        !retry.sequence && !background.lease.seq &&
        !sysfileclosewait(&pool, &work, &cache, &anchor, &foreground, &background, mask) &&
        !__sys_sigmask(SIGNAL_SETMASK, mask, &ignored) && !foreground.lease.seq &&
        !background.lease.seq &&
        sysfilerefview(&pool, work.state.self, closing, &reference) == -ERR_STALE);
  querypipes(fd, 1);
  /* Leave the read reference to the service's LIFE path. */
  pipeiotrace[0] = 7;
}

static Connwork *closepeer;
static Handle    closerpc;
static u32       closerestart;

static void
closecaught(int signum)
{
  Msg    gate = {{PIPETEST, 0, 0, 0}};
  Ipcmsg result;
  if (signum == SIGUSR1) {
    closerpc = closepeer->state.operations[6].rpc;
    CHECK(closerpc && dup(0) == 1);
    if (closerestart)
      CHECK(!ipc_call(closepeer->state.call, &gate, CALL_CONTROL, &result));
    caught++;
  }
}

static void
closeclient(u32 mode)
{
  Fileimage            *image;
  Connwork             *peer;
  Conncache            *directory;
  Pipecreate            pipe = {0};
  Fdview                old, view;
  Fileref               ref;
  Msg                   gate = {{PIPETEST, 0, 0, 0}};
  Ipcmsg                result;
  struct sigaction      action = {0};
  const struct timespec pause = {0, 30000000};
  Handle                rpc;
  int                   fds[2];
  isize                 business;
  CHECK(!sysfileimage(&image) && !sysfileimagefs(0, ADDRESS, &peer));
  directory = sysfileimagecache(image);
  closepeer = peer;
  closerestart = mode == 2;
  action.sa_handler = closecaught;
  action.sa_flags = closerestart ? SA_RESTART : 0;
  CHECK(!sigaction(SIGUSR1, &action, NULL));
  submitpipe(&image->contexts, peer, directory, &image->files, &image->fds, &pipe, 1);
  CHECK(!sysfilepipefinish(&image->files, peer, directory, &image->fds, &pipe, fds, &business) &&
        !business && !sysfdobserve(&image->fds, fds[1], &old));
  if (closerestart)
    CHECK(!close(fds[1]) && caught == 1 && closerpc && !peer->state.operations[6].rpc);
  else {
    CHECK(close(fds[1]) == -1 && errno == (mode ? EINPROGRESS : EIO) && caught == !!mode);
    rpc = peer->state.operations[6].rpc;
    CHECK(rpc && (!mode || rpc == closerpc) && peer->state.operations[6].file == old.file &&
          !ipc_call(peer->state.call, &gate, CALL_CONTROL, &result) && !nanosleep(&pause, NULL) &&
          !peer->state.operations[6].rpc);
  }
  CHECK(sysfilerefview(&image->files, peer->state.self, old.file, &ref) == -ERR_STALE &&
        !sysfdobserve(&image->fds, fds[1], &view));
  if (mode)
    CHECK(view.tag.state == SYSFD_LIVE && view.tag.generation > old.tag.generation &&
          view.file != old.file && !close(fds[1]));
  CHECK(close(fds[1]) == -1 && errno == EBADF);
  pipeiotrace[0] = 7;
}

static void
saturatedclient(void)
{
  Fileimage   *image;
  Connwork    *peer;
  Conncache   *cache;
  Pipecreate   pipe = {0};
  Rkey         active = {0};
  Opkey        key;
  Fileioresult completed;
  u64          mask, ignored;
  isize        business;
  int          fds[2];
  char         byte = 0;
  CHECK(!sysfileimage(&image) && !sysfileimagefs(0, ADDRESS, &peer) &&
        !sysfileimagepipe(0, peer, &pipe, &active, fds, &business) && !business);
  cache = sysfileimagecache(image);
  CHECK(!__sys_sigmask(SIGNAL_BLOCK, SIGNAL_SUPPORTED, &mask) &&
        !sysfileiostage(image, 0, fds[0], FS_READ, NULL, 1, &peer, &key) &&
        !sysctxopcall(&image->contexts, peer->state.self, 0, peer, cache, &key, 0, &active) &&
        !sysctxinvoke(&image->contexts, peer->state.self, active) &&
        !sysctxhandoff(&image->contexts, peer->state.self, active) &&
        !sysctxfinish(&image->contexts, peer->state.self, active));
  CHECK(read(fds[0], &byte, 1) == -1 && errno == EAGAIN && !byte && write(fds[1], "w", 1) == 1 &&
        !sysfileiowait(image, 0, &key, mask, NULL) &&
        !sysfileiofinish(image, &key, &byte, 1, &completed) && !completed.native &&
        !completed.business && completed.count == 1 && byte == 'w' &&
        !__sys_sigmask(SIGNAL_SETMASK, mask, &ignored));
  pipeiotrace[0] = 7;
}

static volatile int brokenwrites;
static int          brokenfd;

static void
brokenpipe(int signal)
{
  CHECK(signal == SIGPIPE && write(brokenfd, NULL, 0) == 0);
  brokenwrites++;
  errno = EDOM;
}

static void
pollbothclient(const Bootview *boot)
{
  Fileimage    *image;
  Connwork     *first, *second;
  Conncache    *cache;
  Connpeer      peer;
  Conntag       tag;
  Connclaim     claim;
  Bootmap       map = {0};
  Pipecreate    pipe = {0};
  Rkey          active = {0};
  struct pollfd rows[3];
  Ipcmsg        result;
  Msg           gate = {{PIPETEST, 0, 0, 0}};
  int           left[2], right[2];
  isize         business;
  Handle        call = bootresource(boot, 257), redundant = 0;
  CHECK(!sysfileimage(&image));
  pipeiotrace[0] = sysfileimagefs(0, ADDRESS, &first);
  CHECK(!pipeiotrace[0]);
  CHECK(!sysfileimagepipe(0, first, &pipe, &active, left, &business));
  CHECK(!business);
  cache = sysfileimagecache(image);
  CHECK(!sysconnpeer(call, &peer) && !sysconnbind(cache, peer, &tag));
  second = &image->connections[tag.index];
  CHECK(first != second && !sysconnworkinit(second, cache, tag));
  CHECK(!sysconnworktakecall(second, cache, &call, &redundant) && !call && !redundant);
  CHECK(!sysconnbegin(cache, tag, image->contexts.sequence + 1, &claim) &&
        !sysbootprepare(&second->state.bootstrap, claim, FS_HELLO, UINT64_MAX, 0));
  CHECK(!sysctxhello(&image->contexts, 0, second, cache, bootresource(boot, BOOT_SELF_SPACE),
                     ADDRESS + FS_CONN_BYTES, &map, &active));
  CHECK(!sysctxpipecreate(&image->contexts, 0, &image->files, second, cache, &image->fds, &pipe,
                          &active, right, &business) &&
        !business);

  rows[0].fd = right[1];
  rows[0].events = POLLOUT;
  rows[0].revents = 0;
  rows[1].fd = left[1];
  rows[1].events = POLLOUT;
  rows[1].revents = 0;
  rows[2] = rows[0];
  CHECK(poll(rows, ARRAY_SIZE(rows), 0) == 3 && rows[0].revents == POLLOUT &&
        rows[1].revents == POLLOUT && rows[2].revents == POLLOUT);
  rows[0].fd = right[0];
  rows[0].events = POLLIN;
  rows[1].fd = left[0];
  rows[1].events = POLLIN;
  rows[2] = rows[0];
  CHECK(poll(rows, ARRAY_SIZE(rows), 250) == 1 && !rows[0].revents && rows[1].revents == POLLIN &&
        !rows[2].revents);

  rows[0].revents = rows[1].revents = rows[2].revents = POLLPRI;
  CHECK(poll(rows, ARRAY_SIZE(rows), 0) == -1 && errno == EIO && rows[0].revents == POLLPRI &&
        rows[1].revents == POLLPRI && rows[2].revents == POLLPRI &&
        !ipc_call(second->state.call, &gate, CALL_CONTROL, &result));
  CHECK(!close(left[0]) && !close(left[1]) && !close(right[0]) && !close(right[1]));
  pipeiotrace[0] = 7;
}

static void
pollfaultclient(int pending)
{
  Fileimage    *image;
  Connwork     *work = NULL;
  Rkey          active = {0};
  Pipecreate    pipe = {0};
  Fdview        fd;
  Fileref       ref;
  Ipcmsg        result;
  Msg           gate = {{PIPETEST, 0, 0, 0}};
  Connentry     entry;
  struct pollfd row;
  int           fds[2];
  isize         business;
  u64           before, after;
  CHECK(!sysfileimage(&image) && !sysfileimagefs(0, ADDRESS, &work) &&
        !sysfileimagepipe(0, work, &pipe, &active, fds, &business) && !business &&
        !sysfdobserve(&image->fds, fds[0], &fd));
  row.fd = fds[0];
  row.events = POLLIN;
  row.revents = POLLPRI;
  if (pending) {
    CHECK(!systimernow(&before) && poll(&row, 1, 0) == -1 && errno == EIO && !systimernow(&after) &&
          after - before >= SYSOP_CONTROL_NS && row.revents == POLLPRI);
    CHECK(!sysconnview(sysfileimagecache(image), work->state.tag, &entry) &&
          entry.state == SYSCONN_READY && !work->state.ended &&
          !sysfilerefview(&image->files, image->files.self, fd.file, &ref) && ref.pins == 1 &&
          !ipc_call(work->state.call, &gate, CALL_CONTROL, &result));
    row.fd = fds[1];
    row.events = POLLOUT;
    CHECK(poll(&row, 1, 0) == 1 && row.revents == POLLOUT &&
          !sysfilerefview(&image->files, image->files.self, fd.file, &ref) && !ref.pins);
    row.fd = fds[0];
    row.events = POLLIN;
  } else {
    /* Observe exit through the native CALL without updating the file cache. */
    CHECK(ipc_call(work->state.call, &gate, CALL_CONTROL, &result) == -ERR_PEER);
  }
  CHECK(poll(&row, 1, -1) == 1 && row.revents == POLLERR && work->state.ended &&
        !sysconnview(sysfileimagecache(image), work->state.tag, &entry) &&
        entry.state == SYSCONN_DRAINING);
  CHECK(poll(&row, 1, 0) == 1 && row.revents == POLLERR);
  pipeiotrace[0] = 7;
}

static void
brokenclient(void)
{
  Connwork        *peer;
  Pipecreate       pipe = {0};
  Rkey             active = {0};
  struct sigaction action = {0};
  sigset_t         blocked, pending, mask;
  isize            business;
  int              fds[2], alias;
  char             byte = 0;
  struct pollfd    ready[3];
  u64              before, after;
  CHECK(!sysfileimagefs(0, ADDRESS, &peer) &&
        !sysfileimagepipe(0, peer, &pipe, &active, fds, &business) && !business);
  ready[0].fd = fds[0];
  ready[0].events = POLLIN;
  ready[1].fd = fds[1];
  ready[1].events = POLLOUT;
  ready[2].fd = -1;
  ready[2].events = POLLIN;
  CHECK(poll(ready, 3, 0) == 1 && !ready[0].revents && ready[1].revents == POLLOUT &&
        !ready[2].revents && !systimernow(&before) && !poll(ready, 1, 10) && !ready[0].revents &&
        !systimernow(&after) && after - before >= 10000000 && !poll(NULL, 0, 5));
  alias = dup(fds[1]);
  CHECK(alias >= 0 && !close(fds[1]) && write(alias, "e", 1) == 1 && !close(alias) &&
        poll(ready, 3, 0) == 2 && ready[0].revents == (POLLIN | POLLHUP) &&
        ready[1].revents == POLLNVAL && !ready[2].revents && read(fds[0], &byte, 1) == 1 &&
        byte == 'e' && read(fds[0], &byte, 1) == 0 && !close(fds[0]));
  CHECK(!sysfileimagepipe(0, peer, &pipe, &active, fds, &business) && !business);
  alias = dup(fds[0]);
  CHECK(alias >= 0 && !close(fds[0]) && write(fds[1], "r", 1) == 1 && read(alias, &byte, 1) == 1 &&
        byte == 'r' && !close(alias));
  brokenfd = fds[1];
  action.sa_handler = brokenpipe;
  CHECK(!sigaction(SIGPIPE, &action, NULL) && write(brokenfd, "x", 1) == -1 && errno == EPIPE &&
        brokenwrites == 1);
  CHECK(!sigemptyset(&blocked) && !sigaddset(&blocked, SIGPIPE) &&
        !sigprocmask(SIG_BLOCK, &blocked, &mask) && write(brokenfd, "x", 1) == -1 &&
        errno == EPIPE && brokenwrites == 1 && !sigpending(&pending) &&
        sigismember(&pending, SIGPIPE) == 1 && !sigprocmask(SIG_SETMASK, &mask, NULL) &&
        brokenwrites == 2);
  action.sa_handler = SIG_IGN;
  CHECK(!sigaction(SIGPIPE, &action, NULL) && write(brokenfd, "x", 1) == -1 && errno == EPIPE &&
        brokenwrites == 2 && !sigpending(&pending) && !sigismember(&pending, SIGPIPE));
  action.sa_handler = SIG_DFL;
  CHECK(!sigaction(SIGPIPE, &action, NULL));
  pipeiotrace[0] = 7;
  write(brokenfd, "x", 1);
  CHECK(0);
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

static const isize openerrors[] = {-ESTALE, -EACCES, 0, -EACCES, -EINVAL, -EINVAL, -EINVAL, -EINTR};

typedef struct Openfixture Openfixture;
struct Openfixture
{
  Loadtable loads;
  Vmounts   mounts;
  Vnode     root, file;
  Fsfiles   files;
  Cache     cache;
  Readfetch fetch;
  u32       holds, attempts, searches, completed, blocked, cancelled;
};

static isize
openhold(void *context, Vnode *node)
{
  Openfixture *fixture = context;
  isize        error = vmounthold(&fixture->mounts, node->mount);
  if (!error)
    fixture->holds++;
  return error;
}

static void
opendrop(void *context, Vnode *node)
{
  Openfixture *fixture = context;
  CHECK(fixture->holds && !vmountdrop(&fixture->mounts, node->mount));
  fixture->holds--;
}

static isize
openwalkhold(void *context, Vnode *node)
{
  Openjob *job = context;
  return openhold(job->backend, node);
}

static void
openwalkdrop(void *context, Vnode *node)
{
  Openjob *job = context;
  opendrop(job->backend, node);
}

static isize
opensearch(void *context, const Vnode *node)
{
  Openjob     *job = context;
  Openfixture *fixture = job->backend;
  CHECK(node == &fixture->root && job->cred.uid == 22 && job->cred.gid == 32 && job->mask == 027);
  return vaccess(&job->cred, 22, 32, fixture->searches++ == 2 ? 0010 : 0100, VACCESS_X, 1);
}

static isize
openlookup(void *context, const Vnode *dir, const char *name, u32 bytes, Vnode **out)
{
  Openjob     *job = context;
  Openfixture *fixture = job->backend;
  CHECK(dir == &fixture->root && bytes == 1 && name[0] == 'x');
  if (fixture->blocked)
    return VWALK_WAIT;
  if (!fixture->attempts++) {
    volatile char *page = (volatile char *) (job->link->address + FS_DATA_OFFSET +
                                             job->call.words[3] * FS_SLOT_BYTES);
    page[FS_SLOT_BYTES - 2] = 'y';
    return VWALK_WAIT;
  }
  CHECK(!openhold(fixture, &fixture->file));
  *out = &fixture->file;
  return 0;
}

static isize
openparent(void *context, const Vnode *node, Vnode **out)
{
  (void) context;
  (void) node;
  (void) out;
  return -EIO;
}

static isize
openlink(void *context, const Vnode *node, const char **target, u64 *bytes)
{
  (void) context;
  (void) node;
  (void) target;
  (void) bytes;
  return -EIO;
}

static void
openvalidate(const Openjob *job)
{
  CHECK(job->link->watch.active.requests[job->call.words[3]] == job->call.request);
}

static isize
openselect(Openjob *job, const Fscontext *context, Vnode **start, u32 *follow)
{
  CHECK(*start == context->cwd && *follow == 1);
  openvalidate(job);
  return fsfileoptions(&job->slot);
}

static isize
opencancel(Openjob *job)
{
  Openfixture *fixture = job->backend;
  openvalidate(job);
  CHECK(fixture->blocked && fixture->holds == 5);
  if (!fixture->cancelled++)
    return -ERR_WOULD_BLOCK;
  return 0;
}

static isize
opencomplete(Openjob *job, Vnode *node)
{
  Openfixture *fixture = job->backend;
  u32          attempt = fixture->completed++;
  openvalidate(job);
  CHECK(node == &fixture->file && fixture->holds == 3 && attempt < 4);
  return fsfileopen(&fixture->files, job, node, 22, 32, attempt > 1 ? 0600 : attempt ? 0400 : 0200);
}

static void
openseek(u64 session, u64 whence, isize offset, isize status, u64 position)
{
  Filelease lease;
  Fileslot  slot = {0};
  Ipcmsg    result;
  Msg       request = {{FS_SEEK, work.state.map.connid, 0, 0}};
  CHECK(!sysconnworkreserve(&work, &cache, SYSFILE_MAIN, 1, &lease));
  slot.seq = request.words[2] = lease.seq;
  request.words[3] = lease.slot;
  slot.state = FS_SLOT_SUBMITTED;
  slot.span = 1;
  slot.object = session;
  slot.args[0] = (u64) offset;
  slot.args[1] = whence;
  CHECK(!__sys_copy((void *) (work.state.map.address + FS_DESC_OFFSET + lease.slot * FS_DESC_BYTES),
                    &slot, sizeof(slot)) &&
        !ipc_call(work.state.call, &request, 0, &result) && (isize) result.words[0] == status &&
        result.words[1] == lease.seq && result.words[2] == position && !result.words[3] &&
        !sysconnworkrelease(&work, &cache, lease));
}

static isize
openreadbind(void *context, Readjob *job)
{
  static const Loadmap map = {{2, 1, 1, 20}, {5}, 0};
  Openfixture         *fixture = context;
  CHECK(job->file->node == &fixture->file);
  job->map = &map;
  job->cache = &fixture->cache;
  job->loads = &fixture->loads;
  job->first = 4;
  return 0;
}

static void
openreadrelease(void *context, Readjob *job)
{
  Openfixture *fixture = context;
  CHECK(job->file->node == &fixture->file && !job->work.fill.cache && !job->work.span.cache);
}

static isize
openreadsubmit(void *context, Readjob *job, const Readfetch *fetch)
{
  Openfixture *fixture = context;
  CHECK(!fixture->fetch.block && fetch->block == 5 && job->file->position == 19);
  fixture->fetch = *fetch;
  return 0;
}

static isize
openlength(void *context, const Vnode *node, u64 *length)
{
  Openfixture *fixture = context;
  CHECK(node == &fixture->file);
  *length = 20;
  return 0;
}

static void
openwritecheck(u64 session, u64 offset, u64 flags, isize expected, u64 accepted)
{
  Filelease    lease;
  Fileslot     slot = {0};
  Ipcmsg       result;
  Msg          request = {{FS_WRITE, work.state.map.connid, 0, 0}};
  volatile u8 *page;
  CHECK(!sysconnworkreserve(&work, &cache, SYSFILE_MAIN, 1, &lease));
  request.words[2] = slot.seq = lease.seq;
  request.words[3] = lease.slot;
  slot.state = FS_SLOT_SUBMITTED;
  slot.span = 1;
  slot.object = session;
  slot.args[0] = offset;
  slot.args[1] = 2;
  slot.args[2] = FS_SLOT_BYTES - 2;
  slot.args[3] = flags;
  slot.payload_bytes = FS_SLOT_BYTES;
  page = (volatile u8 *) (work.state.map.address + FS_DATA_OFFSET + lease.slot * FS_SLOT_BYTES);
  page[FS_SLOT_BYTES - 2] = 'W';
  page[FS_SLOT_BYTES - 1] = 0;
  CHECK(!__sys_copy((void *) (work.state.map.address + FS_DESC_OFFSET + lease.slot * FS_DESC_BYTES),
                    &slot, sizeof(slot)) &&
        !ipc_call(work.state.call, &request, 0, &result) && (isize) result.words[0] == expected &&
        result.words[1] == lease.seq && result.words[2] == accepted && !result.words[3] &&
        page[FS_SLOT_BYTES - 2] == 'W' && !page[FS_SLOT_BYTES - 1] &&
        !sysconnworkrelease(&work, &cache, lease));
}

static void
openwritestage(void)
{
  static Writeimage image;
  static u8         tail[FS_BLOCK_BYTES];
  const Loadmap     map = {{2, 1, 1, 20}, {5}, 0};
  Writeplan         plan;
  u32               i;
  for (i = 0; i < sizeof(tail); i++)
    tail[i] = 0xa5;
  CHECK(!loadmapwrite(&map, NULL, 4, 16, 2 * FS_BLOCK_BYTES + 3, 2, &plan));
  CHECK(loadwritestage(&plan, 4, 16, 5, NULL, tail, "xy", &image) == -ERR_INVALID && !image.block &&
        !image.tail);
  CHECK(!loadwritestage(&plan, 4, 16, 6, NULL, tail, "xy", &image) && image.block == 6 &&
        image.tail == 5);
  for (i = 0; i < FS_BLOCK_BYTES; i++) {
    CHECK(image.data[i] == (i == 3 ? 'x' : i == 4 ? 'y' : 0));
    CHECK(image.taildata[i] == (i < 20 ? 0xa5 : 0) && tail[i] == 0xa5);
  }
}

static void
openserver(const Bootview *boot)
{
  static Filelink link;
  Openjob         job = {0};
  Openfixture     fixture = {0};
  const Vwalkops  ops = {openwalkhold, openwalkdrop, opensearch, openlookup, openparent, openlink};
  const Vwalkops  pin = {openhold, opendrop, NULL, NULL, NULL, NULL};
  Fsfile          file = {0};
  Fsref           refs[2] = {{0}};
  Cacheblock      blocks[1];
  u8              scratch[CACHE_BYTES] = {0};
  Readjob         reader[2] = {0};
  Fsreads         reads = {reader,          ARRAY_SIZE(reader), 0, &fixture, scratch, openreadbind,
                           openreadrelease, openreadsubmit};
  Pipetable       pipes = {0};
  Fscontext       context = {0};
  Fscontexts      contexts = {&context, 1, &pin, &fixture, 0};
  Fsopen          open = {&contexts, &fixture.mounts, &ops,       &job,       1,
                          0,         &fixture,        openselect, opencancel, opencomplete};
  Fsservice       fs = {bootresource(boot, 256),
                        bootresource(boot, BOOT_SELF_ALLOC),
                        bootresource(boot, BOOT_SELF_SPACE),
                        ADDRESS,
                        &link,
                        1,
                        {&pipes, NULL, 0, NULL, 0, &fixture.files, &reads},
                        &open,
                        0};
  u32             i;
  const Loadfile  executable = {2, 1, 1, 20};
  u64             load;
  openwritestage();
  fixture.files = (Fsfiles){&file, refs, 1, ARRAY_SIZE(refs), 0, &pin, &fixture, openlength};
  CHECK(!cacheinit(&fixture.cache, blocks, ARRAY_SIZE(blocks), 16));
  fixture.root = (Vnode){0, 1, 1, VNODE_DIR};
  CHECK(!vmountadd(&fixture.mounts, &fixture.root, NULL));
  fixture.file = (Vnode){fixture.root.mount, 2, 1, VNODE_FILE};
  CHECK(!fsstep(&fs) && link.ready && !fsstep(&fs) && !fixture.holds && !fixture.searches);
  CHECK(!fscontextadd(&contexts, link.watch.owner.pid, link.watch.owner.image, &fixture.root,
                      &fixture.root, 027));
  for (i = 1; i + 1 < ARRAY_SIZE(openerrors); i++)
    CHECK(!fsstep(&fs));
  CHECK(fixture.completed == 2 && fixture.holds == 3 && fixture.attempts == 3 && file.refs == 1 &&
        fixture.files.serial == 2 && file.node == &fixture.file && file.access == VACCESS_R);
  fixture.blocked = 1;
  CHECK(!fsstep(&fs) && job.waiting && fixture.holds == 5);
  while (job.call.request)
    CHECK(!fsstep(&fs));
  CHECK(fixture.cancelled == 2 && fixture.completed == 2 && !fsstep(&fs) && file.refs == 2 &&
        refs[0].file == refs[1].file && refs[0].id != refs[1].id && !fsstep(&fs) &&
        file.refs == 1 && fixture.holds == 3);
  CHECK(!fsstep(&fs) && !fsstep(&fs) && !file.position && file.refs == 1 && !file.head &&
        !fixture.fetch.block);
  for (i = 0; i < 5; i++)
    CHECK(!fsstep(&fs));
  CHECK(file.position == 19 && !fsstep(&fs) && fixture.fetch.block && file.refs == 2 &&
        !fsstep(&fs) && file.position == 19 && file.refs == 3 && !fsstep(&fs));
  {
    Ipcmsg event;
    CHECK(!ipc_recv(EVENT_RQ_CANCEL, RECV_EVENTS_ONLY, &event) &&
          !fsreadpump(&reads, &fixture.files) && file.position == 19 && file.refs == 2 &&
          file.head == &reader[0] && file.tail == file.head && reader[0].work.fill.cache &&
          !reader[1].file);
  }
  CHECK(!fsstep(&fs) && file.position == 19 && file.refs == 3 && !fsstep(&fs) && file.refs == 2 &&
        file.node);
  scratch[19] = 'R';
  scratch[3] = 'P';
  CHECK(fsreadcomplete(&reader[0], reader[1].call.request, &fixture.fetch.ticket, scratch,
                       sizeof(scratch)) == -ERR_STALE &&
        fsreadcomplete(&reader[0], reader[0].call.request, &fixture.fetch.ticket, scratch, 19) ==
            -ERR_INVALID &&
        !fsreadcomplete(&reader[0], reader[0].call.request, &fixture.fetch.ticket, scratch,
                        sizeof(scratch)) &&
        !fsreadpump(&reads, &fixture.files) && !file.node && !file.refs);
  fixture.blocked = 0;
  CHECK(!fsstep(&fs) && file.refs == 1 && !file.position && !fsstep(&fs) && file.refs == 2);
  CHECK(!fsstep(&fs) && file.position == 19 && !fsstep(&fs) && file.position == 19 &&
        !fsstep(&fs) && file.position == 19 && !fsstep(&fs) && file.position == 20 &&
        !fsstep(&fs) && file.position == 20);
  CHECK(!fsstep(&fs) && file.position == 19 &&
        !loadopen(&fixture.loads, &executable, link.watch.owner.pid, link.watch.owner.image, 0,
                  UINT64_MAX, &load) &&
        !fsstep(&fs) && file.position == 19 && !blocks[0].dirty &&
        !loadclose(&fixture.loads, load, link.watch.owner.pid, link.watch.owner.image));
  CHECK(!fsstep(&fs) && file.position == 19 && blocks[0].dirty && !fsstep(&fs) && !fsstep(&fs) &&
        file.position == 20 && !fsstep(&fs) && !fsstep(&fs) && !fsstep(&fs) &&
        file.position == 20 && !fsstep(&fs) && file.refs == 1 && !fsstep(&fs) && !file.node);
  CHECK(!fsstep(&fs) && file.access == (VACCESS_R | VACCESS_W) &&
        file.flags == (FS_OPEN_APPEND | FS_OPEN_NONBLOCK) && !file.position && !fsstep(&fs) &&
        file.refs == 2 && !fsstep(&fs) && !file.position && !fsstep(&fs) && !file.position &&
        !fsstep(&fs) && !file.position && !fsstep(&fs) && file.refs == 1 && !fsstep(&fs) &&
        !file.node && !file.refs &&
        !fscontextremove(&contexts, link.watch.owner.pid, link.watch.owner.image) &&
        !fixture.holds);
  pipeiotrace[0] = 7;
}

static void
openreadcheck(u64 session, u64 offset, int positional, u64 bytes, char first)
{
  Filelease      lease;
  Fileslot       slot = {0};
  Ipcmsg         result;
  Msg            request = {{FS_READ, work.state.map.connid, 0, 0}};
  volatile char *page;
  CHECK(!sysconnworkreserve(&work, &cache, SYSFILE_MAIN, 1, &lease));
  request.words[2] = slot.seq = lease.seq;
  request.words[3] = lease.slot;
  slot.state = FS_SLOT_SUBMITTED;
  slot.span = 1;
  slot.object = session;
  slot.payload_bytes = slot.args[1] = 2;
  slot.args[0] = offset;
  slot.args[3] = positional ? FS_IO_POSITIONAL : 0;
  page = (volatile char *) (work.state.map.address + FS_DATA_OFFSET + lease.slot * FS_SLOT_BYTES);
  page[0] = page[1] = '?';
  CHECK(!__sys_copy((void *) (work.state.map.address + FS_DESC_OFFSET + lease.slot * FS_DESC_BYTES),
                    &slot, sizeof(slot)) &&
        !ipc_call(work.state.call, &request, 0, &result) && !result.words[0] &&
        result.words[1] == lease.seq && result.words[2] == bytes && result.words[3] == bytes &&
        page[0] == first && page[1] == (bytes == 2 ? 0 : '?') &&
        !sysconnworkrelease(&work, &cache, lease));
}

static void
openclient(const Bootview *boot)
{
  u32     i;
  int     append = 0;
  Pipekey key = {0}, dup, reopened = {0};
  setup(boot, &work, &cache, &bank);
open:
  for (i = reopened.reference ? 1 : 0; i < ARRAY_SIZE(openerrors); i++) {
    Filelease      lease;
    Ipcmsg         result;
    Fileslot       slot = {0};
    Msg            request = {{FS_OPEN, work.state.map.connid, 0, 0}};
    volatile char *page;
    CHECK(!sysconnworkreserve(&work, &cache, SYSFILE_MAIN, 1, &lease));
    request.words[2] = slot.seq = lease.seq;
    request.words[3] = lease.slot;
    slot.state = FS_SLOT_SUBMITTED;
    slot.span = 1;
    slot.payload_bytes = FS_SLOT_BYTES;
    slot.args[0] = reopened.reference ? FS_OPEN_RDWR : FS_OPEN_RDONLY;
    if (append)
      slot.args[0] |= FS_OPEN_APPEND | FS_OPEN_NONBLOCK | FS_OPEN_CLOEXEC;
    slot.args[2] = i == 4 ? UINT64_MAX : FS_SLOT_BYTES - 3;
    slot.args[3] = 3;
    slot.args[4] = i == 6;
    page = (volatile char *) (work.state.map.address + FS_DATA_OFFSET + lease.slot * FS_SLOT_BYTES);
    page[FS_SLOT_BYTES - 3] = i == 5 ? 0 : '/';
    page[FS_SLOT_BYTES - 2] = 'x';
    page[FS_SLOT_BYTES - 1] = 0;
    CHECK(
        !__sys_copy((void *) (work.state.map.address + FS_DESC_OFFSET + lease.slot * FS_DESC_BYTES),
                    &slot, sizeof(slot)));
    if (!reopened.reference && i + 1 == ARRAY_SIZE(openerrors)) {
      Msg    gate = {{FS_ATTACH, 0, 0, 0}};
      Ipcmsg drain;
      Handle rpc;
      CHECK(!ipc_call(work.state.call, &request, CALL_ASYNC, &result));
      rpc = result.request;
      CHECK(!ipc_call(work.state.call, &gate, CALL_CONTROL, &drain) && !ipc_rqcancel(rpc) &&
            !ipc_call(work.state.call, &gate, CALL_CONTROL, &drain) &&
            !ipc_rqwait(rpc, 0, &result) && !ipc_rqwait(rpc, RQ_FINISH, &drain) &&
            !ipc_rqack(rpc, ACK_RESULT | ACK_RELEASE) &&
            !ipc_call(work.state.call, &gate, CALL_CONTROL, &drain));
    } else
      CHECK(!ipc_call(work.state.call, &request, 0, &result));
    CHECK((isize) result.words[0] == (reopened.reference ? 0 : openerrors[i]) &&
          result.words[1] == lease.seq);
    if (reopened.reference || !openerrors[i]) {
      CHECK((result.words[2] & FS_FILE_SESSION) && result.words[3] == sizeof(u64));
      key.session = result.words[2];
      key.reference = *(const volatile u64 *) page;
      CHECK(key.reference);
    } else
      CHECK(!result.words[2] && !result.words[3]);
    CHECK(!sysconnworkrelease(&work, &cache, lease));
    if (reopened.reference)
      break;
  }
  dup.session = key.session;
  if (reopened.reference) {
    CHECK(key.session != reopened.session);
    dup.reference = refcall(&work, &cache, FS_DUPREF, key);
    CHECK(dup.reference && dup.reference != key.reference);
    if (append) {
      openwritecheck(dup.session, 0, 0, -ENOSYS, 0);
      openwritecheck(key.session, 0, FS_IO_APPEND, -ENOSYS, 0);
      openwritecheck(dup.session, 3, FS_IO_POSITIONAL, 0, 2);
      CHECK(!refcall(&work, &cache, FS_CLOSE, dup) && !refcall(&work, &cache, FS_CLOSE, key));
      pipeiotrace[0] = 7;
      return;
    }
    openseek(key.session, FS_SEEK_SET, 19, 0, 19);
    openreadcheck(dup.session, 3, 1, 2, 'P');
    openseek(key.session, FS_SEEK_CUR, 0, 0, 19);
    openreadcheck(key.session, 0, 0, 1, 'R');
    openreadcheck(dup.session, 0, 0, 0, '?');
    openseek(key.session, FS_SEEK_SET, 19, 0, 19);
    openwritecheck(dup.session, 0, 0, -ETXTBSY, 0);
    openwritecheck(dup.session, 3, FS_IO_POSITIONAL, 0, 2);
    openseek(key.session, FS_SEEK_CUR, 0, 0, 19);
    openwritecheck(key.session, 0, 0, 0, 1);
    openreadcheck(dup.session, 3, 1, 2, 'W');
    openreadcheck(dup.session, 19, 1, 1, 'W');
    openwritecheck(key.session, 0, FS_IO_APPEND, -EINVAL, 0);
    CHECK(!refcall(&work, &cache, FS_CLOSE, dup) && !refcall(&work, &cache, FS_CLOSE, key));
    reopened = key;
    append = 1;
    goto open;
  }
  dup.reference = refcall(&work, &cache, FS_DUPREF, key);
  CHECK(dup.reference && dup.reference != key.reference && !refcall(&work, &cache, FS_CLOSE, key));
  openwritecheck(dup.session + 1, 0, 0, -ESTALE, 0);
  openwritecheck(dup.session, 0, 0, -EBADF, 0);
  openseek(dup.session, FS_SEEK_SET, 9, 0, 9);
  openseek(dup.session, FS_SEEK_CUR, 3, 0, 12);
  openseek(dup.session, FS_SEEK_END, -1, 0, 19);
  openseek(dup.session, FS_SEEK_CUR, -40, -EINVAL, 0);
  openseek(dup.session, FS_SEEK_CUR, 0, 0, 19);
  {
    Filelease      lease, queued;
    Fileslot       slot = {0};
    Msg            request = {{FS_READ, work.state.map.connid, 0, 0}};
    Ipcmsg         result, drain;
    Handle         rpc, seekrpc;
    volatile char *page;
    CHECK(!sysconnworkreserve(&work, &cache, SYSFILE_MAIN, 1, &lease));
    request.words[2] = slot.seq = lease.seq;
    request.words[3] = lease.slot;
    slot.state = FS_SLOT_SUBMITTED;
    slot.span = 1;
    slot.object = dup.session;
    slot.payload_bytes = slot.args[1] = 1;
    page = (volatile char *) (work.state.map.address + FS_DATA_OFFSET + lease.slot * FS_SLOT_BYTES);
    page[0] = 'P';
    CHECK(
        !__sys_copy((void *) (work.state.map.address + FS_DESC_OFFSET + lease.slot * FS_DESC_BYTES),
                    &slot, sizeof(slot)) &&
        !ipc_call(work.state.call, &request, CALL_ASYNC, &result));
    rpc = result.request;
    CHECK(!sysconnworkreserve(&work, &cache, SYSFILE_MAIN, 1, &queued));
    request.words[0] = FS_SEEK;
    request.words[2] = slot.seq = queued.seq;
    request.words[3] = queued.slot;
    slot.payload_bytes = 0;
    slot.args[0] = 3;
    slot.args[1] = FS_SEEK_CUR;
    CHECK(!__sys_copy(
              (void *) (work.state.map.address + FS_DESC_OFFSET + queued.slot * FS_DESC_BYTES),
              &slot, sizeof(slot)) &&
          !ipc_call(work.state.call, &request, CALL_ASYNC, &result));
    seekrpc = result.request;
    {
      Msg gate = {{FS_ATTACH, 0, 0, 0}};
      CHECK(!ipc_call(work.state.call, &gate, CALL_CONTROL, &drain) && !ipc_rqcancel(seekrpc) &&
            !ipc_rqwait(seekrpc, 0, &result) && (isize) result.words[0] == -EINTR &&
            result.words[1] == queued.seq && !ipc_rqwait(seekrpc, RQ_FINISH, &drain) &&
            !ipc_rqack(seekrpc, ACK_RESULT | ACK_RELEASE) &&
            ipc_rqpoll(rpc, RQ_FINISH, &drain) == -ERR_WOULD_BLOCK && page[0] == 'P' &&
            !sysconnworkrelease(&work, &cache, queued));
    }
    CHECK(!sysconnworkreserve(&work, &cache, SYSFILE_MAIN, 1, &queued));
    request.words[2] = slot.seq = queued.seq;
    request.words[3] = queued.slot;
    CHECK(!__sys_copy(
              (void *) (work.state.map.address + FS_DESC_OFFSET + queued.slot * FS_DESC_BYTES),
              &slot, sizeof(slot)) &&
          !ipc_call(work.state.call, &request, CALL_ASYNC, &result));
    seekrpc = result.request;
    CHECK(page[0] == 'P' && !refcall(&work, &cache, FS_CLOSE, dup) &&
          !ipc_rqwait(rpc, 0, &result) && !result.words[0] && result.words[1] == lease.seq &&
          result.words[2] == 1 && result.words[3] == 1 && page[0] == 'R' &&
          !ipc_rqwait(rpc, RQ_FINISH, &drain) && !ipc_rqack(rpc, ACK_RESULT | ACK_RELEASE) &&
          !sysconnworkrelease(&work, &cache, lease) && !ipc_rqwait(seekrpc, 0, &result) &&
          !result.words[0] && result.words[1] == queued.seq && result.words[2] == 23 &&
          !result.words[3] && !ipc_rqwait(seekrpc, RQ_FINISH, &drain) &&
          !ipc_rqack(seekrpc, ACK_RESULT | ACK_RELEASE) &&
          !sysconnworkrelease(&work, &cache, queued));
  }
  reopened = key;
  goto open;
}

static void
lateopenserver(const Bootview *boot)
{
  Filelink              link = {0};
  Fileslot              row;
  Ipcmsg                request, gate, close;
  Msg                   reply = {{0, 0, 0, 0}};
  Sigsel                target = {SIGNAL_SELECT_PID, 0};
  const struct timespec pause = {0, 30000000};
  u64                   flags;
  CHECK(!ipc_recv(0, 0, &request) &&
        !sysfilehello(&link, 1, bootresource(boot, 256), bootresource(boot, BOOT_SELF_ALLOC),
                      bootresource(boot, BOOT_SELF_SPACE), ADDRESS, &request));
  CHECK(!ipc_recv(0, 0, &request) && request.words[0] == FS_OPEN &&
        !sysfilelinkaccept(&link, 1, &request, &row) && !ipc_defer(request.request));
  target.target = request.source;
  CHECK(!__sys_sigsend(&target, SIGNAL_USR1) && !ipc_recv(0, 0, &gate) &&
        gate.source == target.target && gate.words[0] == PIPETEST &&
        !ipc_rqcheck(request.request, &flags) && flags == (RQ_CHECK_LIVE | RQ_CHECK_CANCEL));
  *(volatile u64 *) (link.address + FS_DATA_OFFSET + request.words[3] * FS_SLOT_BYTES) = 123;
  reply.words[1] = request.words[2];
  reply.words[2] = 456;
  reply.words[3] = sizeof(u64);
  CHECK(!ipc_reply(gate.request, &reply) && !nanosleep(&pause, NULL) &&
        !ipc_reply(request.request, &reply) &&
        !sysfilefinish(&link.watch.active, request.request) && !ipc_recv(0, 0, &close) &&
        close.words[0] == FS_CLOSE && !sysfilelinkaccept(&link, 1, &close, &row) &&
        row.object == 456 && row.args[0] == 123);
  reply.words[1] = close.words[2];
  reply.words[2] = reply.words[3] = 0;
  CHECK(!ipc_reply(close.request, &reply) && !sysfilefinish(&link.watch.active, close.request) &&
        !sysfilelinkclose(&link));
  pipeiotrace[0] = 7;
}

static void
lateopensignal(int signal)
{
  CHECK(signal == SIGUSR1);
  caught++;
}

static void
lateopenclient(void)
{
  Fileimage       *image;
  Connwork        *peer;
  Opencreate       open = {0};
  Rkey             active = {0};
  struct sigaction action = {0};
  Msg              gate = {{PIPETEST, 0, 0, 0}};
  Ipcmsg           reply;
  Fdview           fd;
  int              number = -2;
  isize            business = 9;
  u64              mask, ignored, deadline, now;
  action.sa_handler = lateopensignal;
  CHECK(!sigaction(SIGUSR1, &action, NULL) && !sysfileimage(&image) &&
        !sysfileimagefs(0, ADDRESS, &peer));
  CHECK(sysfileimageopen(0, peer, "/late", 0, 0, 0, &open, &active, &number, &business) ==
            -ERR_TIMEDOUT &&
        caught == 1 && !open.operation.lease.seq && !active.sequence && number == -2 &&
        business == 9 && !sysfdobserve(&image->fds, 0, &fd) && fd.tag.state == SYSFD_FREE &&
        !ipc_call(peer->state.call, &gate, CALL_CONTROL, &reply) &&
        !__sys_sigmask(SIGNAL_BLOCK, SIGNAL_SUPPORTED, &mask) && !systimernow(&now));
  deadline = now + UINT64_C(500000000);
  do {
    CHECK(!systimerwait(deadline, EVENT_TIMER, mask) && !systimernow(&now) && now < deadline);
  } while (peer->state.operations[0].phase || peer->state.operations[1].phase ||
           sysfilerefconnection(&image->files, peer->state.self, peer->state.tag));
  CHECK(!__sys_sigmask(SIGNAL_SETMASK, mask, &ignored));
  pipeiotrace[0] = 7;
}

int
main(int argc, char **argv)
{
  const Bootview *boot = sysboot();
  CHECK(boot && argc == 1);
  if (argv[0][0] == 's')
    server(boot);
  else if (argv[0][0] == 'O')
    openserver(boot);
  else if (argv[0][0] == 'o')
    openclient(boot);
  else if (argv[0][0] == 'W')
    lateopenserver(boot);
  else if (argv[0][0] == 'w')
    lateopenclient();
  else if (argv[0][0] == 'S' || argv[0][0] == 'B')
    serviceloop(boot, 0);
  else if (argv[0][0] == 'b')
    brokenclient();
  else if (argv[0][0] == 'D')
    serviceloop(boot, 5);
  else if (argv[0][0] == 'd')
    saturatedclient();
  else if (argv[0][0] == 'F')
    serviceloop(boot, 6);
  else if (argv[0][0] == 'f')
    pollfaultclient(1);
  else if (argv[0][0] == 'G')
    serviceloop(boot, 7);
  else if (argv[0][0] == 'g')
    pollfaultclient(0);
  else if (argv[0][0] == 'H' || argv[0][0] == 'J')
    serviceloop(boot, argv[0][0] == 'H' ? 8 : 9);
  else if (argv[0][0] == 'h')
    pollbothclient(boot);
  else if (argv[0][0] == 'I')
    serviceloop(boot, 1);
  else if (argv[0][0] == 'i')
    interruptclient();
  else if (argv[0][0] == 'C')
    serviceclient(boot);
  else if (argv[0][0] == 'T' || argv[0][0] == 'U')
    serviceloop(boot, argv[0][0] == 'T' ? 2 : 3);
  else if (argv[0][0] == 't' || argv[0][0] == 'u')
    closeclient(argv[0][0] == 'u');
  else if (argv[0][0] == 'R')
    serviceloop(boot, 4);
  else if (argv[0][0] == 'r')
    closeclient(2);
  else
    run(boot);
  return 42;
}
