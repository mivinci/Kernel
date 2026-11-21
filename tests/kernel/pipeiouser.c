#include <errno.h>
#include <sys/boot.h>
#include <sys/channel.h>
#include "private/connection.h"
#include "private/filewatch.h"
#include "private/start.h"
#include "private/syscall.h"
#include "../../sbin/fs/pipefile.h"
#include "../../sbin/fs/pipeio.h"

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
zero(void *data, u64 bytes)
{
  u64 i;
  for (i = 0; i < bytes; i++)
    ((u8 *) data)[i] = 0;
}

static void
setup(const Bootview *boot, Connwork *work, Conncache *cache)
{
  Connpeer  peer;
  Conntag   tag;
  Connclaim claim;
  Bootmap   map;
  Msg       message;
  Ipcmsg    result;
  Handle    call, parent, redundant = 0, rpc;
  CHECK(!__sys_getid(&cache->self.pid, &cache->self.image, &parent));
  CHECK(!sysconninit(cache, cache->self));
  call = bootresource(boot, 256);
  CHECK(!sysconnpeer(call, &peer) && !sysconnbind(cache, peer, &tag) &&
        !sysconnworkinit(work, cache, tag) &&
        !sysconnworktakecall(work, cache, &call, &redundant) && !call && !redundant &&
        !sysconnbegin(cache, tag, 1, &claim) &&
        !sysbootprepare(&work->state.bootstrap, claim, FS_HELLO, UINT64_MAX, 0) &&
        !sysbootrequest(&work->state.bootstrap, claim, &message) &&
        !ipc_call(work->state.call, &message, CALL_ASYNC | CALL_RECV_MEM, &result));
  rpc = result.request;
  CHECK(!sysbootsubmitted(&work->state.bootstrap, claim, &result.request) &&
        !ipc_rqwait(rpc, 0, &result) && !sysbootcapture(&work->state.bootstrap, claim, 0, &result));
  zero(&map, sizeof(map));
  CHECK(!sysbootmap(&work->state.bootstrap, claim, bootresource(boot, BOOT_SELF_SPACE), ADDRESS,
                    peer.image, &map) &&
        !sysconnworkadopt(work, cache, claim, bootresource(boot, BOOT_SELF_SPACE), &map) &&
        !sysbootack(&work->state.bootstrap, claim) &&
        !sysbootfinish(&work->state.bootstrap, claim));
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
  static Pipeio   io;
  Piperef         refs[2] = {{0}};
  Pipetable       table = {files, refs, ARRAY_SIZE(files), ARRAY_SIZE(refs), 0};
  Pipekey         keys[2];
  Pipefile       *pinned;
  Pipe           *pipe = &files[0].stream;
  Filelink        link = {0};
  Fileinflight   *active = &link.watch.active;
  Ipcmsg          input;
  Fileslot        slot;
  Msg             reply = {{0, 0, 0, 0}};
  u32             attempt = 0, phase = 0, end, i;
  volatile u8    *page;
  CHECK(!ipc_recv(0, 0, &input) &&
        !sysfilehello(&link, 1, bootresource(boot, 256), bootresource(boot, BOOT_SELF_ALLOC),
                      bootresource(boot, BOOT_SELF_SPACE), ADDRESS, &input));
  filelifetime(link.watch.owner.pid, link.watch.owner.image);
  CHECK(!ipc_recv(0, 0, &input) && input.words[0] == FS_PIPE &&
        !sysfilelinkaccept(&link, 1, &input, &slot) && !slot.object && !slot.payload_bytes &&
        slot.span == 1 && !slot.args[0] && slot.args[1] == PIPE_CAPACITY);
  CHECK(!pipefilecreate(&table, input.source, input.image, 0, keys));
  {
    volatile u64 *data =
        (volatile u64 *) (link.address + FS_DATA_OFFSET + input.words[3] * FS_SLOT_BYTES);
    Msg created = {{0, slot.seq, 2, 32}};
    for (i = 0; i < 2; i++) {
      data[i * 2] = keys[i].session;
      data[i * 2 + 1] = keys[i].reference;
    }
    CHECK(!ipc_reply(input.request, &created) && !sysfilefinish(active, input.request));
  }
  for (;;) {
    CHECK(!ipc_recv(0, 0, &input) && input.kind == IPC_CALL &&
          input.source == link.watch.owner.pid && input.image == link.watch.owner.image);
    if (input.words[0] == PIPETEST) {
      CHECK(input.words[1] == phase);
      switch (phase++) {
      case 0:
      case 2:
        CHECK(io.request && !io.fixed);
        break;
      case 1:
        CHECK(io.request && !pipeiostep(&io, pipe, active) && !io.request && !pipe->bytes &&
              !pipedrop(pipe, PIPE_READ));
        break;
      case 3:
        CHECK(io.request && *io.page == 'x' && piperead(pipe, fill, sizeof(fill)) == sizeof(fill) &&
              !pipeiostep(&io, pipe, active) && !io.request && pipe->bytes == 1 &&
              !pipedrop(pipe, PIPE_WRITE));
        break;
      case 4:
        CHECK(attempt == 5 && !pipe->bytes &&
              !pipefileclose(&table, input.source, input.image, keys[PIPE_WRITE]));
        break;
      case 5:
        CHECK(attempt == 6 && !pipefileclose(&table, input.source, input.image, keys[PIPE_READ]) &&
              !pipefilecreate(&table, input.source, input.image, 0, keys) &&
              !pipefileclose(&table, input.source, input.image, keys[PIPE_READ]));
        reply.words[2] = keys[PIPE_WRITE].session;
        break;
      case 6:
        CHECK(attempt == 7 && !io.request && !pipe->bytes &&
              !pipefileclose(&table, input.source, input.image, keys[PIPE_WRITE]));
        for (i = 0; i < FS_CONN_SLOTS; i++)
          CHECK(!active->requests[i]);
        CHECK(!ipc_reply(input.request, &reply));
        return;
      default:
        CHECK(0);
      }
      CHECK(!ipc_reply(input.request, &reply));
      continue;
    }
    CHECK(attempt < 7 && !sysfilelinkaccept(&link, 1, &input, &slot));
    end = attempt == 3 || attempt == 6 ? PIPE_WRITE : PIPE_READ;
    CHECK(!pipefilepin(&table, input.source, input.image, slot.object, end, &pinned) &&
          &pinned->stream == pipe);
    page = (volatile u8 *) (link.address + FS_DATA_OFFSET + input.words[3] * FS_SLOT_BYTES);
    CHECK(
        !pipeiostart(&io, active, &input, &slot, page, end, pinned->nonblock[end] || attempt == 1));
    if (attempt == 3)
      CHECK(pipewrite(pipe, fill, sizeof(fill)) == sizeof(fill));
    if (!attempt || attempt == 3)
      CHECK(pipeiostep(&io, pipe, active) == -ERR_WOULD_BLOCK && !ipc_defer(io.request));
    else
      CHECK(!pipeiostep(&io, pipe, active) && !io.request && !pipedrop(pipe, end));
    pipeiotrace[0] = ++attempt;
  }
}

static void
run(const Bootview *boot)
{
  const isize      status[] = {-EINTR, -EAGAIN, -ESPIPE, 0, 0, 0, -EPIPE};
  static Connwork  work;
  static Conncache cache;
  Fileslot         slot = {0};
  Filelease        lease;
  Ipcmsg           result;
  Msg              call = {{FS_READ, 0, 0, 0}}, gate = {{PIPETEST, 0, 0, 0}};
  volatile u8     *page;
  Handle           rpc;
  u32              attempt;
  u64              sessions[2];
  volatile u64    *created;
  setup(boot, &work, &cache);
  slot.state = FS_SLOT_SUBMITTED;
  slot.span = 1;
  CHECK(!sysconnworkreserve(&work, &cache, SYSFILE_MAIN, 1, &lease));
  call.words[0] = FS_PIPE;
  call.words[1] = work.state.map.connid;
  call.words[2] = slot.seq = lease.seq;
  call.words[3] = lease.slot;
  slot.args[1] = PIPE_CAPACITY;
  CHECK(!__sys_copy((void *) (work.state.map.address + FS_DESC_OFFSET + lease.slot * FS_DESC_BYTES),
                    &slot, sizeof(slot)) &&
        !ipc_call(work.state.call, &call, CALL_ASYNC, &result));
  rpc = result.request;
  CHECK(!ipc_rqwait(rpc, 0, &result) && !result.words[0] && result.words[1] == lease.seq &&
        result.words[2] == 2 && result.words[3] == 32 && !ipc_rqwait(rpc, RQ_FINISH, &result));
  created = (volatile u64 *) (work.state.map.address + FS_DATA_OFFSET + lease.slot * FS_SLOT_BYTES);
  sessions[PIPE_READ] = created[0];
  sessions[PIPE_WRITE] = created[2];
  CHECK(sessions[0] && sessions[1] && sessions[0] != sessions[1] && created[1] && created[3] &&
        created[1] != created[3] && !ipc_rqack(rpc, ACK_RESULT | ACK_RELEASE) &&
        !sysconnworkrelease(&work, &cache, lease));
  slot.payload_bytes = FS_SLOT_BYTES;
  slot.args[1] = 1;
  slot.args[2] = FS_SLOT_BYTES - 1;
  for (attempt = 0; attempt < ARRAY_SIZE(status); attempt++) {
    if (attempt == 5 || attempt == 6) {
      CHECK(!ipc_call(work.state.call, &gate, CALL_CONTROL, &result));
      if (attempt == 6)
        sessions[PIPE_WRITE] = result.words[2];
      gate.words[1]++;
    }
    CHECK(!sysconnworkreserve(&work, &cache, SYSFILE_MAIN, 1, &lease));
    call.words[0] = attempt == 3 || attempt == 6 ? FS_WRITE : FS_READ;
    call.words[1] = work.state.map.connid;
    slot.object = sessions[call.words[0] == FS_READ ? PIPE_READ : PIPE_WRITE];
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
}

int
main(int argc, char **argv)
{
  const Bootview *boot = sysboot();
  CHECK(boot && argc == 1);
  if (argv[0][0] == 's')
    server(boot);
  else
    run(boot);
  return 42;
}
