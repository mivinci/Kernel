#include "loadio.h"
#include <errno.h>

static u64    state;
static Handle request;
static Msg    result;
static u32    replies, finishes;
static isize  failure;

isize
ipc_rqcheck(Handle expected, u64 *out)
{
  if (expected != request)
    return -ERR_STALE;
  *out = state;
  return 0;
}

isize
ipc_reply(Handle expected, const Msg *reply)
{
  if (expected != request || !(state & RQ_CHECK_LIVE))
    return -ERR_STATE;
  replies++;
  result = *reply;
  return failure;
}

isize
ipc_rqfinish(Handle expected)
{
  if (expected != request || state != RQ_CHECK_DRAINING)
    return -ERR_STATE;
  finishes++;
  return failure;
}

#define CHECK(c)                                                                                   \
  do {                                                                                             \
    if (!(c))                                                                                      \
      return __LINE__;                                                                             \
  } while (0)

int
loadtest(u32 scenario)
{
  union
  {
    u64 align;
    u8  bytes[FS_CONN_BYTES];
  } region = {0};
  Loadtable    table = {0};
  Loadio       io = {0};
  Fileinflight active = {{0}};
  Loadfile     file = {6, 7, 8, 8192};
  Ipcmsg       call = {0};
  Fileslot     wire = {0}, accepted;
  u64          session, guard;
  u8          *page = region.bytes + FS_DATA_OFFSET;
  const u8     staged[4] = {1, 2, 3, 4};
  u32          i;

  request = ((Handle) HANDLE_REQUEST << 59) | ((Handle) 1 << 16) | 5;
  state = RQ_CHECK_LIVE;
  replies = finishes = 0;
  failure = 0;
  CHECK(!loadopen(&table, &file, 9, 10, 1, 12, &session));
  call.kind = IPC_CALL;
  call.source = 9;
  call.image = 10;
  call.request = request;
  call.words[0] = FS_LOAD;
  call.words[1] = 65536;
  call.words[2] = 4;
  wire.seq = 4;
  wire.state = FS_SLOT_SUBMITTED;
  wire.payload_bytes = FS_SLOT_BYTES;
  wire.object = session;
  wire.span = 1;
  wire.args[0] = file.inode;
  wire.args[1] = file.generation;
  wire.args[2] = file.version;
  wire.args[3] = file.length;
  wire.args[4] = file.length - sizeof(staged);
  wire.args[5] = sizeof(staged);
  wire.args[6] = FS_SLOT_BYTES - sizeof(staged);
  for (i = 0; i < FS_SLOT_BYTES; i++)
    page[i] = 0xa5;
  for (i = 0; i < sizeof(wire); i++)
    region.bytes[FS_DESC_OFFSET + i] = ((const u8 *) &wire)[i];

  CHECK(loadiostart(&io, &table, &active, &call, &wire, page, 2) == -ERR_STALE);
  CHECK(!sysfileaccept(&active, region.bytes, sizeof(region.bytes), 0, 4, request, &accepted));
  if (scenario == 0) {
    accepted.args[6]++;
    CHECK(loadiostart(&io, &table, &active, &call, &accepted, page, 2) == -ERR_INVALID);
    accepted.args[6]--;
    accepted.args[7] = UINT64_MAX;
    CHECK(loadiostart(&io, &table, &active, &call, &accepted, page, 2) == -ERR_INVALID);
    accepted.args[7] = 0;
    call.image++;
    CHECK(loadiostart(&io, &table, &active, &call, &accepted, page, 2) == -ERR_DENIED);
    call.image--;
    accepted.args[2]++;
    CHECK(loadiostart(&io, &table, &active, &call, &accepted, page, 2) == -ERR_STALE);
    accepted.args[2]--;
    state |= RQ_CHECK_CANCEL;
    CHECK(loadiostart(&io, &table, &active, &call, &accepted, page, 2) == -ERR_CANCELLED);
    CHECK(active.requests[0] == request && !io.request && !replies && !finishes);
    return 0;
  }
  CHECK(!loadiostart(&io, &table, &active, &call, &accepted, page, 2));
  CHECK(io.offset == file.length - sizeof(staged) && io.file.version == file.version);
  CHECK(loadwrite(&table, file.inode, &guard) == -ERR_BUSY);
  /* Later descriptor changes cannot redirect completion. */
  accepted.args[6] = 0;
  region.bytes[FS_DESC_OFFSET] = 0xff;
  if (scenario == 1 || scenario == 6) {
    failure = -ERR_INTERRUPTED;
    CHECK(loadioreply(&io, staged, sizeof(staged), 3) == -ERR_INTERRUPTED);
    CHECK(io.ticket && active.requests[0] == request && replies == 1);
    CHECK(page[0] == 0xa5 && page[FS_SLOT_BYTES - 5] == 0xa5 && page[FS_SLOT_BYTES - 4] == 1 &&
          page[FS_SLOT_BYTES - 1] == 4);
    page[FS_SLOT_BYTES - 1] = 0x55;
    failure = 0;
    CHECK(!loadioreply(&io, NULL, 0, scenario == 6 ? 12 : 3));
    CHECK(result.words[1] == 4 && replies == 2 && page[FS_SLOT_BYTES - 1] == 0x55);
    if (scenario == 6)
      CHECK((isize) result.words[0] == -ETIMEDOUT && !result.words[2] && !result.words[3]);
    else
      CHECK(!result.words[0] && result.words[2] == 4 && result.words[3] == 4);
  } else if (scenario == 2) {
    state |= RQ_CHECK_CANCEL;
    CHECK(!loadioreply(&io, staged, sizeof(staged), 3));
    CHECK((isize) result.words[0] == -EINTR && result.words[1] == 4 && !result.words[2] &&
          !result.words[3] && replies == 1);
  } else if (scenario == 3) {
    loadretire(&table, 9, 10);
    state = RQ_CHECK_DRAINING;
    failure = -ERR_INTERRUPTED;
    CHECK(loadioreply(&io, staged, sizeof(staged), 3) == -ERR_INTERRUPTED);
    CHECK(active.requests[0] == request && finishes == 1);
    CHECK(loadwrite(&table, file.inode, &guard) == -ERR_BUSY);
    failure = 0;
    CHECK(!loadioreply(&io, NULL, 0, 3) && finishes == 2 && !replies);
    CHECK(!loadwrite(&table, file.inode, &guard));
  } else if (scenario == 4) {
    CHECK(!loadioreply(&io, staged, sizeof(staged), 12));
    CHECK((isize) result.words[0] == -ETIMEDOUT && !result.words[2] && !result.words[3]);
  } else {
    CHECK(!loadioreply(&io, staged, sizeof(staged) - 1, 3));
    CHECK((isize) result.words[0] == -EIO && !result.words[2] && !result.words[3]);
  }
  if (scenario != 1 && scenario != 6)
    for (i = 0; i < FS_SLOT_BYTES; i++)
      CHECK(page[i] == 0xa5);
  CHECK(!io.request && !active.requests[0]);
  CHECK(loadioreply(&io, NULL, 0, 3) == -ERR_STALE);
  return 0;
}
