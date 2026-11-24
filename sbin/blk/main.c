#include "start.h"
#include "private/start.h"
#include "private/syscall.h"
#include "private/virtio/mmio.h"
#include <abi/ipc.h>
#include <abi/time.h>
#include <errno.h>
#include <proto/init.h>
#include <unistd.h>

#define QUEUE_GENERATION UINT64_C(1)

typedef struct Blkdiagnostic Blkdiagnostic;
struct Blkdiagnostic
{
  u32 phase, shutdownstage;
  u64 error, serial, readydeadline, wake;
  u32 mapped, stopping, shutdown, readycount, irqe;
};
typedef struct Blkrun Blkrun;
struct Blkrun
{
  Blkstart boot;
  Blkstate state;
  u64      serial, readydeadline, wake;
  Handle   pid, parent, session, init, ready, shutdown;
  u64      shutdownsequence;
  u32      phase, mapped, stopping, readycount, irqe;
};

/* Private debugger/diagnostic state, never an authority source. */
volatile Blkdiagnostic blkdiag;
static Blkrun          blkrun;

static int
coherentsync(void *context, u32 region, u32 phase, u64 offset, u64 length)
{
  UNUSED(context);
  UNUSED(region);
  UNUSED(offset);
  UNUSED(length);
  /* QEMU virt exposes coherent guest RAM. A noncoherent production transport
     needs a separately frozen capability and synchronization path. */
  virtiofence(NULL, phase);
  return 0;
}

static int
notify(void *context)
{
  return vnotify(context, 0);
}

static int
blksync(void *context, u32 phase, const Vbuf *buffer)
{
  UNUSED(buffer);
  virtiofence(context, phase == VR_BEFORE ? VR_RELEASE : VR_ACQUIRE);
  return 0;
}

static u64
now(void)
{
  int64_t seconds;
  u64     nanos;
  isize   error = __sys_clock_gettime(ABI_CLOCK_MONOTONIC, &seconds, &nanos);
  if (error || seconds < 0 || nanos >= UINT64_C(1000000000) ||
      (u64) seconds > (UINT64_MAX - nanos) / UINT64_C(1000000000))
    return 0;
  return (u64) seconds * UINT64_C(1000000000) + nanos;
}

static NORETURN void
failed(u32 stage, isize error)
{
  if (blkrun.ready)
    (void) ipc_rqcancel(blkrun.ready);
  blkdiag.phase = 0xff;
  blkdiag.readycount = stage;
  blkdiag.error = (u64) error;
  _exit((int) stage);
}

static void
diagnostic(u32 stage, isize error)
{
  blkdiag.phase = 0xff;
  blkdiag.error = (u64) error;
  blkdiag.readycount = stage;
}

static isize
mapping(Handle memory, u64 address, u64 bytes)
{
  Maparg args = {memory, 0, address, bytes, MAP_READ | MAP_WRITE, 0};
  return __sys_map(blkrun.boot.handle[BlkSpace], &args);
}

static void
readyresult(void)
{
  Ipcmsg reply;
  u64    current;
  isize  error;
  if (!blkrun.ready)
    return;
  error = ipc_rqpoll(blkrun.ready, 0, &reply);
  if (error == -ERR_WOULD_BLOCK) {
    current = now();
    if (!current || current >= blkrun.readydeadline)
      failed(121, -ERR_TIMEDOUT);
    return;
  }
  if (error || reply.kind || reply.source || reply.request || reply.image || reply.words[0] ||
      reply.words[1] != 1 || reply.words[2] != blkrun.boot.number ||
      reply.words[3] != blkrun.boot.image)
    failed(122, error ? error : -ERR_PEER);
  error = ipc_rqack(blkrun.ready, ACK_RESULT | ACK_RELEASE);
  if (error)
    failed(123, error);
  blkrun.ready = 0;
  blkrun.phase = 4;
}

static isize
shutdowncall(const Ipcmsg *call)
{
  Msg   reply = {{(u64) -EPERM, call->words[2], 0, 0}};
  u64   state;
  isize error = ipc_rqcheck(call->request, &state);
  if (error)
    return error;
  if (state == RQ_CHECK_DRAINING)
    return ipc_rqfinish(call->request);
  if (call->source != 1 || !call->image || !blkrun.boot.initimage ||
      call->image != blkrun.boot.initimage || INIT_WORD_VERSION(call->words[0]) != INIT_VERSION ||
      INIT_WORD_OPCODE(call->words[0]) != INIT_SHUTDOWN)
    return ipc_reply(call->request, &reply);
  if (state & RQ_CHECK_CANCEL)
    reply.words[0] = (u64) -EINTR;
  else if (state != RQ_CHECK_LIVE)
    return -ERR_STATE;
  else {
    blkrun.stopping = 1;
    blkrun.shutdown = call->request;
    blkrun.shutdownsequence = call->words[2];
    reply.words[0] = 0;
  }
  return blkrun.stopping == 1 && blkrun.shutdown == call->request
             ? 0
             : ipc_reply(call->request, &reply);
}

static isize
dispatch(Blkservice *service, const Ipcmsg *call)
{
  if (call->source == 1 && call->image && call->image == blkrun.boot.initimage && !call->words[1] &&
      call->words[2] && !call->words[3] && INIT_WORD_VERSION(call->words[0]) == INIT_VERSION &&
      INIT_WORD_OPCODE(call->words[0]) == INIT_SHUTDOWN)
    return shutdowncall(call);
  return blkservicecall(service, call);
}

static u64
preparedeadline(const Vdev *device)
{
  u64 current = now();
  if (!current)
    return 0;
  if (device->state == VDEV_RESETTING) {
    if (!device->deadline)
      return 0;
    return MIN(device->deadline,
               current > UINT64_MAX - BLK_RETRY_NS ? UINT64_MAX : current + BLK_RETRY_NS);
  }
  return current > UINT64_MAX - BLK_RETRY_NS ? UINT64_MAX : current + BLK_RETRY_NS;
}

static isize
prepare(void)
{
  Blkstate *state = &blkrun.state;
  isize     error;
  u32       i;
  error = vmmioinit(&state->mmio, &state->vops, BLK_MMIO_BASE + blkrun.boot.mmiooffset,
                    blkrun.boot.mmiobytes);
  if (error)
    return error;
  error = vprobe(&state->device, &state->vops);
  if (error)
    return error;
  if (state->device.state != VDEV_FOUND || state->device.device != VBLK_DEVICE) {
    diagnostic(120, state->device.device);
    return -ERR_NOTSUP;
  }
  for (;;) {
    u64 current = now();
    if (!current)
      return -ERR_FAULT;
    error = blkqprepare(&state->device, current, &state->config);
    if (!error)
      break;
    if (error != -ERR_WOULD_BLOCK) {
      diagnostic(121, error);
      return error;
    }
    error = __sys_alarmset(preparedeadline(&state->device), &blkrun.serial);
    if (error)
      return error;
    {
      Ipcmsg event;
      error = ipc_recv(EVENT_TIMER | EVENT_RQ_CANCEL, 0, &event);
      if (error && error != -ERR_INTERRUPTED) {
        diagnostic(122, error);
        return error;
      }
    }
  }
  for (i = 0; i < BLK_REQUEST_COUNT; i++) {
    const u64 page = 3 + (u64) i * BLK_RING_PAGES;
    state->headers[i] = (Vblkhead *) (uintptr_t) (BLK_DMA_BASE + page * PAGE_SIZE);
    state->data[i] = (volatile u8 *) (uintptr_t) (BLK_DMA_BASE + (page + 1) * PAGE_SIZE);
    state->status[i] = (volatile u8 *) (uintptr_t) (BLK_DMA_BASE + (page + 2) * PAGE_SIZE);
    state->buffers[i] = (Blkdma){state->headers[i],
                                 state->data[i],
                                 state->status[i],
                                 blkrun.boot.addresses[page],
                                 blkrun.boot.addresses[page + 1],
                                 blkrun.boot.addresses[page + 2],
                                 PAGE_SIZE};
  }
  state->ringmapping[0] = (void *) (uintptr_t) (BLK_DMA_BASE);
  state->ringmapping[1] = (void *) (uintptr_t) (BLK_DMA_BASE + PAGE_SIZE);
  state->ringmapping[2] = (void *) (uintptr_t) (BLK_DMA_BASE + 2 * PAGE_SIZE);
  state->ringops = (Vringops){&state->device, coherentsync, virtiofence, notify};
  error = blkqstart(&state->queue, &state->device, &state->ring, QUEUE_GENERATION,
                    state->ringmapping, blkrun.boot.addresses, &state->ringops, state->requests,
                    state->buffers, ARRAY_SIZE(state->buffers));
  if (error) {
    diagnostic(123, error);
    return error;
  }
  /* DRIVER_OK makes interrupt observation legal. The platform IRQ remains
     masked until negotiation hints are drained and the fixed binding is
     rearmed immediately before service publication. */
  do {
    u32 causes;
    error = vinterrupt(&state->device, &causes);
    if (error)
      return error;
    if (!causes)
      break;
  } while (1);
  error = __sys_irqack(blkrun.boot.handle[BlkIrq]);
  if (error)
    return error;
  error = blktransportinit(&state->transport, &state->queue, state->links, ARRAY_SIZE(state->links),
                           state->jobs, ARRAY_SIZE(state->jobs), blkrun.boot.device + 1, blksync,
                           &state->device);
  if (error) {
    diagnostic(124, error);
    return error;
  }
  state->service = (Blkservice){&state->transport,
                                blkrun.boot.handle[BlkObserve],
                                blkrun.boot.handle[BlkAlloc],
                                blkrun.boot.handle[BlkSpace],
                                blkrun.boot.handle[BlkIrq],
                                BLK_CONN_BASE,
                                blkrun.boot.irqevent,
                                0,
                                0,
                                dispatch};
  return 0;
}

static void
recoverfailed(void)
{
  Ipcmsg event;
  u64    deadline, current, serial;
  isize  error;
  if (!blkrun.state.queue.state)
    return;
  for (;;) {
    current = now();
    if (!current) {
      diagnostic(110, -ERR_FAULT);
      return;
    }
    error = blkqrecover(&blkrun.state.queue, current);
    if (!error)
      return;
    if (error != -ERR_WOULD_BLOCK && error != -ERR_BUSY) {
      diagnostic(111, error);
      return;
    }
    deadline = blkrun.state.device.deadline;
    if (!deadline || current >= deadline) {
      diagnostic(112, -ERR_TIMEDOUT);
      return;
    }
    error = __sys_alarmset(deadline, &serial);
    if (error) {
      diagnostic(113, error);
      return;
    }
    for (;;) {
      error = ipc_recv(EVENT_TIMER | EVENT_RQ_CANCEL, 0, &event);
      if (error != -ERR_INTERRUPTED)
        break;
    }
    if (error) {
      diagnostic(114, error);
      return;
    }
  }
}

int
main(int argc, char **argv, char **envp)
{
  Ipcmsg message;
  Msg    ready = {{INIT_WORD(INIT_READY), 1, 0, 0}};
  u64    current;
  isize  error;
  (void) argc;
  (void) argv;
  (void) envp;
  blkrun.phase = 1;
  error = blkstartboot(sysboot(), &blkrun.boot);
  if (!error)
    error = blkstartcheck(&blkrun.boot);
  if (error)
    failed(100, error);
  blkrun.pid = blkrun.boot.pid;
  blkrun.init = blkrun.boot.handle[BlkInit];
  error = __sys_setsid(&blkrun.session);
  if (!error)
    error = mapping(blkrun.boot.handle[BlkMmio], BLK_MMIO_BASE, PAGE_SIZE);
  if (!error) {
    blkrun.mapped |= 1;
    error = mapping(blkrun.boot.handle[BlkDma], BLK_DMA_BASE, BLK_DMA_PAGES * PAGE_SIZE);
  }
  if (!error) {
    blkrun.mapped |= 2;
    error = prepare();
  }
  if (error) {
    recoverfailed();
    failed(101, error);
  }
  blkrun.phase = 2;
  current = now();
  if (!current || current > UINT64_MAX - BLK_READY_NS)
    failed(102, -ERR_OVERFLOW);
  blkrun.readydeadline = current + BLK_READY_NS;
  error = __sys_alarmset(blkrun.readydeadline, &blkrun.serial);
  if (error)
    failed(103, error);
  ready.words[2] = blkrun.boot.number;
  ready.words[3] = blkrun.boot.image;
  error = ipc_call(blkrun.init, &ready, CALL_ASYNC | CALL_CONTROL, &message);
  if (error || !message.request)
    failed(104, error ? error : -ERR_STATE);
  blkrun.ready = message.request;
  blkrun.phase = 3;
  for (;;) {
    u64 events = blkrun.boot.irqevent | EVENT_LIFE | EVENT_RQ_CANCEL | EVENT_RQ_DONE | EVENT_TIMER;
    if (blkrun.ready)
      readyresult();
    if (blkrun.stopping) {
      current = now();
      if (!current)
        failed(105, -ERR_FAULT);
      error = blkservestop(&blkrun.state.service, current);
      if (error == -ERR_WOULD_BLOCK || error == -ERR_BUSY) {
        u64 deadline = current > UINT64_MAX - BLK_RETRY_NS ? UINT64_MAX : current + BLK_RETRY_NS;
        error = __sys_alarmset(deadline, &blkrun.serial);
      } else if (!error && blkrun.shutdown) {
        error = blktransportreap(&blkrun.state.transport);
        if (error == -ERR_BUSY || error == -ERR_WOULD_BLOCK) {
          u64 deadline = current > UINT64_MAX - BLK_RETRY_NS ? UINT64_MAX : current + BLK_RETRY_NS;
          error = __sys_alarmset(deadline, &blkrun.serial);
        }
        if (!error)
          error = ipc_reply(blkrun.shutdown, &(Msg){{0, blkrun.shutdownsequence, 0, 0}});
        if (!error) {
          blkrun.shutdown = 0;
          blkrun.phase = 6;
        }
      }
      if (error)
        failed(106, error);
      if (!blkrun.shutdown)
        break;
    } else {
      error = blkservestepmask(&blkrun.state.service, events, blkrun.ready ? RECV_NONBLOCK : 0);
      if (error)
        failed(107, error);
      if (blkrun.ready)
        error = __sys_yield();
      if (error)
        failed(108, error);
    }
  }
  error = __sys_alarmcancel(&blkrun.serial);
  if (error && error != -ERR_STALE)
    failed(109, error);
  blkrun.phase = 7;
  return 0;
}
