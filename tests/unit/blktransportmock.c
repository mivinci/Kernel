#include "transport.h"
#include "private/syscall.h"
#include <abi/mem.h>
#include <abi/observe.h>
#include <errno.h>

#define TEST_LINKS  2
#define TEST_JOBS   2
#define TEST_CONN   ((u64) 7 << 16)
#define TEST_IMAGE  UINT64_C(0x2222)
#define TEST_PID    ((Handle) 17)
#define TEST_PORT   UINT64_C(0x100100000)
#define TEST_DATA   UINT64_C(0x100200000)
#define TEST_STATUS UINT64_C(0x100300000)
#define ALIGNED(n)  __attribute__((aligned(n)))
#define CHECK(c)                                                                                   \
  do                                                                                               \
    if (!(c))                                                                                      \
      return __LINE__;                                                                             \
  while (0)

typedef struct Testring  Testring;
typedef struct Testspace Testspace;
struct Testring
{
  Vring          ring;
  Vdesc          desc[VQ_SIZE] ALIGNED(PAGE_SIZE);
  Vavail avail   ALIGNED(PAGE_SIZE);
  Vusedring used ALIGNED(PAGE_SIZE);
};
struct Testspace
{
  u8 bytes[TEST_LINKS * PROTO_CONN_BYTES] ALIGNED(PAGE_SIZE);
};

static Testring     testring;
static Testspace    testspace;
static Vdev         device;
static Vblkconfig   deviceconfig = {2048, 0, 1, 512, 0, 0};
static Filelink     links[TEST_LINKS];
static Blkqueue     queue;
static Blkjob       jobs[TEST_JOBS];
static Blktransport transport;
static Vblkreq      requests[TEST_JOBS];
static Blkdma       buffers[TEST_JOBS];
static Vblkhead     headers[TEST_JOBS];
static volatile u8  statuses[TEST_JOBS];
static u8           data[TEST_JOBS][PROTO_SLOT_BYTES];
static u64          ringgeneration;
static u32          notifications, maps, unmaps, allocations, frees, observations, drops;
static u32          requestchecks, requestdefers, requestreplies, requestfinishes, ledgerfinishes;
static isize        defererror, replyerror, finishrequesterror, finishledgererror;
static Handle       request;
static u64          requeststate;
static Msg          result;

static isize blktransporttestsysfilefinish(Fileinflight *active, Handle expected);

static void
zero(void *base, usize bytes)
{
  u8   *out = base;
  usize i;
  for (i = 0; i < bytes; i++)
    out[i] = 0;
}

static void
copy(void *target, const void *source, usize bytes)
{
  u8       *out = target;
  const u8 *in = source;
  usize     i;
  for (i = 0; i < bytes; i++)
    out[i] = in[i];
}

static int
deviceread(void *context, u32 offset, u32 *out)
{
  const u32 values[] = {1, 2048, 0, 512};
  const u32 offsets[] = {0xfc, 0x100, 0x104, 0x114};
  u32       i;
  (void) context;
  if (offset == 0xfc) {
    *out = 1;
    return 0;
  }
  for (i = 1; i < ARRAY_SIZE(offsets); i++)
    if (offset == offsets[i]) {
      *out = values[i];
      return 0;
    }
  return -ERR_INVALID;
}

static int
devicereadbyte(void *context, u32 offset, u8 *out)
{
  (void) context;
  (void) offset;
  (void) out;
  return -ERR_INVALID;
}

static int
ringsync(void *context, u32 region, u32 phase, u64 offset, u64 length)
{
  (void) context;
  (void) region;
  (void) phase;
  (void) offset;
  (void) length;
  return 0;
}

static void
ringfence(void *context, u32 phase)
{
  (void) context;
  (void) phase;
}

static int
ringnotify(void *context)
{
  (void) context;
  notifications++;
  return 0;
}

static int
datasync(void *context, u32 phase, const Vbuf *buffer)
{
  (void) context;
  (void) phase;
  (void) buffer;
  return 0;
}

static Handle
newrequest(u64 generation)
{
  return ((Handle) HANDLE_REQUEST << 59) | ((Handle) generation << 16) | 5;
}

static int
setupqueue(void)
{
  Vringops  ops = {0, ringsync, ringfence, ringnotify};
  Vdevops   devops = {0, deviceread, 0, devicereadbyte};
  const u64 addresses[3] = {TEST_PORT, TEST_PORT + PAGE_SIZE, TEST_PORT + 2 * PAGE_SIZE};
  u32       i;
  zero(&testring, sizeof(testring));
  zero(&device, sizeof(device));
  device.device = VBLK_DEVICE;
  device.features = VBLK_F_BLK_SIZE | VBLK_F_FLUSH;
  device.ops = devops;
  zero(&queue, sizeof(queue));
  zero(requests, sizeof(requests));
  zero(buffers, sizeof(buffers));
  zero(headers, sizeof(headers));
  for (i = 0; i < TEST_JOBS; i++)
    statuses[i] = 0;
  zero(data, sizeof(data));
  ringgeneration++;
  if (vrinit(&testring.ring, ringgeneration, testring.desc, &testring.avail, &testring.used, &ops))
    return 1;
  device.state = VDEV_LIVE;
  for (i = 0; i < TEST_JOBS; i++) {
    buffers[i].header = &headers[i];
    buffers[i].data = data[i];
    buffers[i].status = &statuses[i];
    buffers[i].headaddr = TEST_DATA + (u64) i * UINT64_C(0x10000);
    buffers[i].dataaddr = buffers[i].headaddr + PAGE_SIZE;
    buffers[i].statusaddr = TEST_STATUS + (u64) i * UINT64_C(0x1000);
    buffers[i].capacity = PROTO_SLOT_BYTES;
    requests[i].buffers[0].address = buffers[i].headaddr;
    requests[i].buffers[1].address = buffers[i].dataaddr;
    requests[i].buffers[2].address = buffers[i].statusaddr;
  }
  if (blkqinit(&queue, &device, &testring.ring, requests, buffers, TEST_JOBS, addresses))
    return __LINE__;
  queue.config = deviceconfig;
  return 0;
}

static void
setupconnection(u32 index)
{
  Filelink *link = &links[index];
  Protohead head = {0};
  u64       connid = TEST_CONN + (u64) index;
  u64       address = (u64) (uintptr_t) testspace.bytes + (u64) index * PROTO_CONN_BYTES;
  zero(link, sizeof(*link));
  zero(testspace.bytes + index * PROTO_CONN_BYTES, PROTO_CONN_BYTES);
  link->watch.owner.pid = TEST_PID;
  link->watch.owner.image = TEST_IMAGE;
  link->watch.owner.cookie = (u64) index + 1;
  link->watch.connid = connid;
  link->memory = (Handle) (0x31 + index);
  link->space = (Handle) (0x41 + index);
  link->address = address;
  link->serverimage = TEST_IMAGE;
  link->ready = 1;
  link->prepaid = 1;
  head.magic = PROTO_CONN_MAGIC;
  head.bytes = PROTO_CONN_BYTES;
  head.connid = connid;
  head.serverimage = TEST_IMAGE;
  head.slots = PROTO_CONN_SLOTS;
  head.slotbytes = PROTO_SLOT_BYTES;
  copy(testspace.bytes + index * PROTO_CONN_BYTES, &head, sizeof(head));
}

static int
resettest(void)
{
  u32 i;
  zero(links, sizeof(links));
  zero(jobs, sizeof(jobs));
  zero(&transport, sizeof(transport));
  zero(testspace.bytes, sizeof(testspace.bytes));
  notifications = maps = unmaps = allocations = frees = observations = drops = 0;
  requestchecks = requestdefers = requestreplies = requestfinishes = ledgerfinishes = 0;
  defererror = replyerror = finishrequesterror = finishledgererror = 0;
  request = 0;
  requeststate = RQ_CHECK_LIVE;
  zero(&result, sizeof(result));
  for (i = 0; i < TEST_LINKS; i++)
    setupconnection(i);
  i = (u32) setupqueue();
  if (i)
    return (int) i;
  if (blktransportinit(&transport, &queue, links, TEST_LINKS, jobs, TEST_JOBS, UINT64_C(0x44),
                       datasync, NULL))
    return __LINE__;
  blktransportfilefinish(&transport, blktransporttestsysfilefinish);
  return 0;
}

static void
preparecall(Ipcmsg *call, Fileslot *slot, u32 link, u32 index, u64 opcode, u64 offset, u64 length)
{
  u64 *shared;
  zero(slot, sizeof(*slot));
  slot->seq = 10;
  slot->state = PROTO_SLOT_SUBMITTED;
  slot->payload_bytes = length;
  slot->span = 1;
  slot->args[0] = offset;
  slot->args[1] = length;
  slot->args[2] = opcode == BLK_INFO ? 0 : queue.geometry;
  zero(call, sizeof(*call));
  call->kind = IPC_CALL;
  call->source = TEST_PID;
  call->image = TEST_IMAGE;
  call->request = request;
  call->words[0] = BLK_WORD(opcode);
  call->words[1] = links[link].watch.connid;
  call->words[2] = slot->seq;
  call->words[3] = index;
  shared = (u64 *) (testspace.bytes + link * PROTO_CONN_BYTES + PROTO_DESC_OFFSET +
                    index * PROTO_DESC_BYTES);
  copy(shared, slot, sizeof(*slot));
}

static Protoslot *
descriptor(u32 link, u32 slot)
{
  return (Protoslot *) (testspace.bytes + link * PROTO_CONN_BYTES + PROTO_DESC_OFFSET +
                        slot * PROTO_DESC_BYTES);
}

static int
publishused(u32 queue_slot, u32 length)
{
  u32 head;
  u16 ring_slot = testring.used.index % VQ_SIZE;
  for (head = 0; head < VQ_SIZE; head++)
    if (testring.ring.queue.slots[head].state == VQ_PUBLISHED &&
        testring.ring.queue.slots[head].cookie == queue.slots[queue_slot].cookie)
      break;
  CHECK(head < VQ_SIZE);
  statuses[queue_slot] = VBLK_OK;
  testring.used.ring[ring_slot].id = head;
  testring.used.ring[ring_slot].length = length;
  testring.used.index++;
  return 0;
}

static int
complete(u32 queue_slot, u32 length)
{
  Vdone done[VQ_BATCH];
  u32   count, more, causes;
  CHECK(!publishused(queue_slot, length));
  CHECK(!blktransportpoll(&transport, done, &count, &more, &causes));
  CHECK(count == 1 && !more);
  return 0;
}

isize
ipc_rqcheck(Handle expected, u64 *state)
{
  requestchecks++;
  if (!request || expected != request)
    return -ERR_STALE;
  *state = requeststate;
  return 0;
}

isize
ipc_defer(Handle expected)
{
  requestdefers++;
  if (expected != request)
    return -ERR_STALE;
  return defererror;
}

isize
ipc_reply(Handle expected, const Msg *reply)
{
  requestreplies++;
  if (expected != request || (requeststate & RQ_CHECK_DRAINING))
    return -ERR_STATE;
  if (!replyerror)
    result = *reply;
  return replyerror;
}

isize
ipc_rqfinish(Handle expected)
{
  if (expected != request || requeststate != RQ_CHECK_DRAINING)
    return -ERR_STATE;
  requestfinishes++;
  return finishrequesterror;
}

isize
blktransporttestsysfilefinish(Fileinflight *active, Handle expected)
{
  u32 i;
  int found = 0;
  if (!active || !expected)
    return -ERR_STALE;
  for (i = 0; i < PROTO_CONN_SLOTS; i++)
    if (active->requests[i] == expected) {
      found = 1;
      if (!finishledgererror)
        active->requests[i] = 0;
    }
  ledgerfinishes++;
  return finishledgererror ? finishledgererror : found ? 0 : -ERR_STALE;
}

isize
__sys_getid(Handle *pid, u64 *image, Handle *parent)
{
  if (!pid || !image || !parent)
    return -ERR_INVALID;
  *pid = 1;
  *image = TEST_IMAGE;
  *parent = 0;
  return 0;
}

isize
__sys_clock_gettime(u64 clock, int64_t *seconds, u64 *nanoseconds)
{
  (void) clock;
  if (!seconds || !nanoseconds)
    return -ERR_INVALID;
  *seconds = 0;
  *nanoseconds = 0;
  return 0;
}

isize
ipc_reply_resource(Handle expected, const Resreply *descriptor)
{
  (void) expected;
  (void) descriptor;
  return -ERR_NOTSUP;
}

isize
__sys_alloc(Handle allocator, const Allocarg *args, Handle *out)
{
  (void) allocator;
  if (!args || !out || args->kind != ALLOC_SHARED)
    return -ERR_INVALID;
  allocations++;
  *out = (Handle) (0x100 + allocations);
  return 0;
}

isize
__sys_map(Handle space, const Maparg *args)
{
  (void) space;
  if (!args)
    return -ERR_INVALID;
  maps++;
  return 0;
}

isize
__sys_unmap(Handle space, u64 address)
{
  (void) space;
  (void) address;
  unmaps++;
  return 0;
}

isize
__sys_free(Handle resource)
{
  (void) resource;
  frees++;
  return 0;
}

isize
__sys_copy(void *target, const void *source, u64 bytes)
{
  copy(target, source, bytes);
  return 0;
}

isize
__sys_observe(Handle permit, Handle basis, u64 relation, Handle pid, u64 image, u64 *cookie,
              u64 *state, u64 *reason)
{
  (void) permit;
  (void) basis;
  (void) relation;
  (void) pid;
  (void) image;
  if (!cookie || !state || !reason)
    return -ERR_INVALID;
  observations++;
  *cookie = observations;
  *state = OBS_ACTIVE;
  *reason = 0;
  return 0;
}

isize
__sys_obsread(u64 cookie, u64 *state, u64 *reason, Handle *pid, u64 *image)
{
  (void) cookie;
  if (!state || !reason || !pid || !image)
    return -ERR_INVALID;
  *state = OBS_ACTIVE;
  *reason = 0;
  *pid = TEST_PID;
  *image = TEST_IMAGE;
  return 0;
}

isize
__sys_obsack(u64 cookie)
{
  (void) cookie;
  return 0;
}

isize
__sys_obsdrop(u64 cookie)
{
  (void) cookie;
  drops++;
  return 0;
}

int
vinterrupt(Vdev *input, u32 *causes)
{
  (void) input;
  if (!causes)
    return -ERR_INVALID;
  *causes = 0;
  return 0;
}

int
vfeatures(Vdev *input, u64 wanted, u64 required)
{
  (void) input;
  (void) wanted;
  (void) required;
  return -ERR_NOTSUP;
}

int
vresetbegin(Vdev *input, u64 now)
{
  (void) input;
  (void) now;
  return -ERR_NOTSUP;
}

int
vresetpoll(Vdev *input, u64 now)
{
  (void) input;
  (void) now;
  return -ERR_NOTSUP;
}

static int
infotest(void)
{
  Ipcmsg   call;
  Fileslot slot;
  Blkinfo  info;
  if (resettest())
    return __LINE__;
  request = newrequest(1);
  preparecall(&call, &slot, 0, 0, BLK_INFO, 0, 0);
  slot.payload_bytes = BLK_INFO_BYTES;
  copy(descriptor(0, 0), &slot, sizeof(slot));
  CHECK(!blktransportcall(&transport, &call));
  CHECK(descriptor(0, 0)->state == PROTO_SLOT_DONE);
  CHECK(result.words[0] == 0);
  CHECK(result.words[1] == call.words[2]);
  CHECK(result.words[2] == queue.geometry);
  CHECK(result.words[3] == BLK_INFO_BYTES);
  CHECK(requestreplies == 1);
  CHECK(ledgerfinishes == 1);
  copy(&info, testspace.bytes + PROTO_DATA_OFFSET, sizeof(info));
  return 0;
}

static int
deferfailuretest(void)
{
  Ipcmsg   call;
  Fileslot slot;
  if (resettest())
    return __LINE__;
  request = newrequest(1);
  preparecall(&call, &slot, 0, 0, BLK_WRITE, 512, 512);
  defererror = -ERR_BUSY;
  CHECK(!blktransportcall(&transport, &call));
  CHECK(requestdefers == 1 && notifications == 0 && !queue.slots[0].active && !transport.active &&
        descriptor(0, 0)->state == PROTO_SLOT_DONE && requestreplies == 1 && ledgerfinishes == 1);
  return 0;
}

static int
writetest(int cancel)
{
  Ipcmsg   call;
  Fileslot slot;
  u32      i;
  if (resettest())
    return __LINE__;
  request = newrequest(1);
  preparecall(&call, &slot, 0, 0, BLK_WRITE, 512, 512);
  for (i = 0; i < 512; i++)
    testspace.bytes[PROTO_DATA_OFFSET + i] = (u8) i;
  CHECK(!blktransportcall(&transport, &call));
  CHECK(requestdefers == 1 && notifications == 1 && queue.slots[0].active &&
        transport.active == 1 && !requestreplies);
  for (i = 0; i < 512; i++)
    CHECK(data[0][i] == (u8) i);
  for (i = 0; i < 512; i++)
    testspace.bytes[PROTO_DATA_OFFSET + i] ^= 0xff;
  if (cancel)
    requeststate |= RQ_CHECK_CANCEL;
  CHECK(!complete(0, 1));
  CHECK(!result.words[0]);
  CHECK(result.words[1] == call.words[2]);
  CHECK(result.words[2] == 512 && result.words[3] == 512);
  CHECK(descriptor(0, 0)->state == PROTO_SLOT_DONE);
  CHECK(requestreplies == 1);
  CHECK(ledgerfinishes == 1);
  return 0;
}

static int
readtest(int cancel)
{
  Ipcmsg   call;
  Fileslot slot;
  u32      i;
  if (resettest())
    return __LINE__;
  request = newrequest(1);
  preparecall(&call, &slot, 0, 0, BLK_READ, 0, 512);
  slot.payload_bytes = 512;
  copy(descriptor(0, 0), &slot, sizeof(slot));
  for (i = 0; i < 512; i++)
    testspace.bytes[PROTO_DATA_OFFSET + i] = 0xa5;
  CHECK(!blktransportcall(&transport, &call));
  CHECK(queue.slots[0].active && !requestreplies);
  for (i = 0; i < 512; i++) {
    data[0][i] = (u8) (i + 1);
    CHECK(testspace.bytes[PROTO_DATA_OFFSET + i] == 0xa5);
  }
  if (cancel)
    requeststate |= RQ_CHECK_CANCEL;
  CHECK(!complete(0, 513));
  if (cancel) {
    CHECK((isize) result.words[0] == -EINTR && !result.words[2] && !result.words[3]);
    for (i = 0; i < 512; i++)
      CHECK(testspace.bytes[PROTO_DATA_OFFSET + i] == 0xa5);
  } else {
    CHECK(!result.words[0] && result.words[1] == call.words[2] && result.words[2] == 512 &&
          result.words[3] == 512);
    for (i = 0; i < 512; i++)
      CHECK(testspace.bytes[PROTO_DATA_OFFSET + i] == (u8) (i + 1));
  }
  CHECK(descriptor(0, 0)->state == PROTO_SLOT_DONE && ledgerfinishes == 1);
  return 0;
}

static int
replyretrytest(void)
{
  Ipcmsg   call;
  Fileslot slot;
  if (resettest())
    return __LINE__;
  request = newrequest(1);
  preparecall(&call, &slot, 0, 0, BLK_WRITE, 0, 512);
  CHECK(!blktransportcall(&transport, &call));
  replyerror = -ERR_BUSY;
  CHECK(!complete(0, 1));
  CHECK(requestreplies == 1 && ledgerfinishes == 0 && jobs[0].request &&
        descriptor(0, 0)->state == PROTO_SLOT_DONE);
  replyerror = 0;
  CHECK(!blktransportpump(&transport));
  CHECK(requestreplies == 2 && ledgerfinishes == 1 && !jobs[0].request);
  return 0;
}

static int
ledgerretrytest(void)
{
  Ipcmsg   call;
  Fileslot slot;
  if (resettest())
    return __LINE__;
  request = newrequest(1);
  preparecall(&call, &slot, 0, 0, BLK_WRITE, 0, 512);
  CHECK(!blktransportcall(&transport, &call));
  finishledgererror = -ERR_INTERRUPTED;
  CHECK(!complete(0, 1));
  CHECK(requestreplies == 1 && ledgerfinishes == 1 && jobs[0].request &&
        descriptor(0, 0)->state == PROTO_SLOT_DONE);
  finishledgererror = 0;
  CHECK(!blktransportpump(&transport));
  CHECK(requestreplies == 1 && ledgerfinishes == 2 && !jobs[0].request);
  return 0;
}

static int
immediateretrytest(void)
{
  Ipcmsg   io, info, next;
  Fileslot slot;
  if (resettest())
    return __LINE__;
  request = newrequest(1);
  preparecall(&io, &slot, 0, 0, BLK_WRITE, 0, 512);
  CHECK(!blktransportcall(&transport, &io) && transport.active == 1);
  request = newrequest(2);
  preparecall(&info, &slot, 1, 1, BLK_INFO, 0, BLK_INFO_BYTES);
  slot.args[1] = 0;
  copy(descriptor(1, 1), &slot, sizeof(slot));
  replyerror = -ERR_BUSY;
  CHECK(blktransportcall(&transport, &info) == -ERR_BUSY);
  CHECK(blktransportbusy(&transport, &links[1]) && transport.active == 1 &&
        descriptor(1, 1)->state == PROTO_SLOT_DONE && requestreplies == 1 && !ledgerfinishes);
  replyerror = 0;
  finishledgererror = -ERR_INTERRUPTED;
  CHECK(blktransportpump(&transport) == -ERR_INTERRUPTED);
  CHECK(requestreplies == 2 && ledgerfinishes == 1 && transport.active == 1 && !result.words[0] &&
        result.words[1] == info.words[2] && result.words[2] == queue.geometry &&
        result.words[3] == BLK_INFO_BYTES);
  request = newrequest(3);
  preparecall(&next, &slot, 1, 2, BLK_INFO, 0, 0);
  next.words[2] = ++slot.seq;
  slot.payload_bytes = BLK_INFO_BYTES;
  copy(descriptor(1, 2), &slot, sizeof(slot));
  CHECK(!blktransportcall(&transport, &next));
  CHECK(requestreplies == 3 && (isize) result.words[0] == -EAGAIN &&
        result.words[1] == next.words[2] && !links[1].watch.active.requests[2] &&
        descriptor(1, 2)->state == PROTO_SLOT_SUBMITTED && blktransportbusy(&transport, &links[1]));
  request = info.request;
  finishledgererror = 0;
  CHECK(!blktransportpump(&transport));
  CHECK(requestreplies == 3 && ledgerfinishes == 3 && transport.active == 1 &&
        !blktransportbusy(&transport, &links[1]) && blktransportbusy(&transport, &links[0]));
  request = io.request;
  CHECK(!complete(0, 1) && !transport.active && !blktransportbusy(&transport, &links[0]));
  return 0;
}

static int
drainingtest(void)
{
  Ipcmsg   call;
  Fileslot slot;
  if (resettest())
    return __LINE__;
  request = newrequest(1);
  preparecall(&call, &slot, 0, 0, BLK_WRITE, 0, 512);
  CHECK(!blktransportcall(&transport, &call));
  requeststate = RQ_CHECK_DRAINING;
  CHECK(!complete(0, 1));
  CHECK(requestfinishes == 1 && requestreplies == 0 && ledgerfinishes == 1 && !jobs[0].request);
  return 0;
}

static int
invalidtest(void)
{
  Ipcmsg   call;
  Fileslot slot;
  if (resettest())
    return __LINE__;
  request = newrequest(1);
  preparecall(&call, &slot, 0, 0, BLK_READ, 1, 512);
  CHECK(!blktransportcall(&transport, &call));
  CHECK(requestdefers == 1 && notifications == 0 && !queue.slots[0].active &&
        descriptor(0, 0)->state == PROTO_SLOT_DONE && (isize) result.words[0] == -EINVAL &&
        requestchecks >= 1);
  if (resettest())
    return __LINE__;
  request = newrequest(2);
  preparecall(&call, &slot, 0, 0, BLK_WRITE, 0, 512);
  call.words[1]++;
  CHECK(!blktransportcall(&transport, &call));
  CHECK(descriptor(0, 0)->state == PROTO_SLOT_SUBMITTED && requestreplies == 1 &&
        (isize) result.words[0] == -ERR_STALE && !requestdefers && !ledgerfinishes);
  return 0;
}

static int
queuetest(void)
{
  Ipcmsg   call;
  Fileslot slot;
  if (resettest())
    return __LINE__;
  request = newrequest(1);
  preparecall(&call, &slot, 0, 0, BLK_READ, 0, 512);
  CHECK(!blktransportcall(&transport, &call));
  request = newrequest(2);
  preparecall(&call, &slot, 1, 1, BLK_READ, 0, 512);
  CHECK(!blktransportcall(&transport, &call));
  CHECK(blktransportbusy(&transport, &links[0]) && blktransportbusy(&transport, &links[1]));
  return 0;
}

static int
completionbatchtest(void)
{
  Ipcmsg   first, second;
  Fileslot slot;
  Vdone    done[VQ_BATCH];
  u32      count, more, causes;
  CHECK(!resettest());
  request = newrequest(1);
  preparecall(&first, &slot, 0, 0, BLK_WRITE, 0, 512);
  CHECK(!blktransportcall(&transport, &first));
  request = newrequest(2);
  preparecall(&second, &slot, 1, 1, BLK_WRITE, 512, 512);
  CHECK(!blktransportcall(&transport, &second));
  CHECK(!publishused(0, 1) && !publishused(1, 1));
  /* The mock lets request checks succeed for only one handle at a time. */
  CHECK(blktransportpoll(&transport, done, &count, &more, &causes) == -ERR_STALE);
  CHECK(jobs[0].pending && queue.slots[0].active && transport.active == 1 &&
        descriptor(0, 0)->state == PROTO_SLOT_SUBMITTED && !jobs[1].request &&
        descriptor(1, 1)->state == PROTO_SLOT_DONE && result.words[1] == second.words[2]);
  request = first.request;
  CHECK(!blktransportpump(&transport) && !transport.active && !queue.slots[0].active &&
        !jobs[0].request && descriptor(0, 0)->state == PROTO_SLOT_DONE &&
        result.words[1] == first.words[2] && requestreplies == 2 && ledgerfinishes == 2);
  return 0;
}

static int
backpressuretest(void)
{
  Ipcmsg   write, flush;
  Fileslot slot;
  if (resettest())
    return __LINE__;
  request = newrequest(1);
  preparecall(&write, &slot, 0, 0, BLK_WRITE, 0, 512);
  CHECK(!blktransportcall(&transport, &write));
  request = newrequest(2);
  preparecall(&flush, &slot, 1, 0, BLK_FLUSH, 0, 0);
  CHECK(!blktransportcall(&transport, &flush));
  CHECK((isize) result.words[0] == -EAGAIN && notifications == 1 && transport.active == 1 &&
        descriptor(1, 0)->state == PROTO_SLOT_DONE);
  request = write.request;
  CHECK(!complete(0, 1));
  request = newrequest(3);
  preparecall(&flush, &slot, 1, 1, BLK_FLUSH, 0, 0);
  CHECK(!blktransportcall(&transport, &flush) && transport.active == 1 && notifications == 2);
  CHECK(!complete(0, 1) && !transport.active && !result.words[0]);

  queue.configpending = 1;
  request = newrequest(4);
  preparecall(&write, &slot, 0, 1, BLK_WRITE, 0, 512);
  CHECK(!blktransportcall(&transport, &write));
  CHECK((isize) result.words[0] == -EAGAIN && notifications == 2 && !transport.active);
  queue.configpending = 0;
  queue.geometry++;
  request = newrequest(5);
  preparecall(&write, &slot, 0, 2, BLK_WRITE, 0, 512);
  slot.args[2]--;
  copy(descriptor(0, 2), &slot, sizeof(slot));
  CHECK(!blktransportcall(&transport, &write));
  CHECK((isize) result.words[0] == -ESTALE && notifications == 2 && !transport.active);
  return 0;
}

static int
linkbusytest(void)
{
  Ipcmsg   call;
  Fileslot slot;
  if (resettest())
    return __LINE__;
  request = newrequest(1);
  preparecall(&call, &slot, 0, 0, BLK_READ, 0, 512);
  CHECK(!blktransportcall(&transport, &call));
  CHECK(blktransportbusy(&transport, &links[0]) && !blktransportbusy(&transport, &links[1]));
  CHECK(!complete(0, 513));
  CHECK(!blktransportbusy(&transport, &links[0]));
  return 0;
}

static isize
reapdrain(void *context, Filelink *link)
{
  Blktransport *target = context;
  if (blktransportbusy(target, link))
    return -ERR_BUSY;
  return 0;
}

static int
reaptest(void)
{
  Ipcmsg   call;
  Fileslot slot;
  if (resettest())
    return __LINE__;
  request = newrequest(1);
  preparecall(&call, &slot, 0, 0, BLK_READ, 0, 512);
  CHECK(!blktransportcall(&transport, &call));
  CHECK(!blktransportreaper(&transport, &transport, reapdrain));
  links[0].watch.owner.reason = 1;
  CHECK(blktransportreap(&transport) == -ERR_BUSY);
  CHECK(!complete(0, 513));
  CHECK(!blktransportreap(&transport));
  CHECK(!links[0].address && !links[0].memory && links[0].watch.owner.cookie == 0);
  return 0;
}

int
blktransporttest(u32 scenario)
{
  switch (scenario) {
  case 0:
    return infotest();
  case 1:
    return deferfailuretest();
  case 2:
    return writetest(0);
  case 3:
    return readtest(0);
  case 4:
    return writetest(1);
  case 5:
    return readtest(1);
  case 6:
    return replyretrytest();
  case 7:
    return ledgerretrytest();
  case 8:
    return drainingtest();
  case 9:
    return invalidtest();
  case 10:
    return queuetest();
  case 11:
    return linkbusytest();
  case 12:
    return reaptest();
  case 13:
    return immediateretrytest();
  case 14:
    return backpressuretest();
  case 15:
    return completionbatchtest();
  default:
    return __LINE__;
  }
}
