#include <errno.h>
#include <sys/boot.h>
#include <sys/channel.h>
#include "private/connection.h"
#include "private/filewatch.h"
#include "private/start.h"
#include "private/syscall.h"
#include "../../sbin/fs/pipefile.h"
#include "../../sbin/fs/pipedispatch.h"

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
  Pipekey          keys[2], dup;
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
  keys[PIPE_READ].session = created[0];
  keys[PIPE_READ].reference = created[1];
  keys[PIPE_WRITE].session = created[2];
  keys[PIPE_WRITE].reference = created[3];
  CHECK(keys[0].session && keys[1].session && keys[0].session != keys[1].session &&
        keys[0].reference && keys[1].reference && keys[0].reference != keys[1].reference &&
        !ipc_rqack(rpc, ACK_RESULT | ACK_RELEASE) && !sysconnworkrelease(&work, &cache, lease));
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
  else
    run(boot);
  return 42;
}
