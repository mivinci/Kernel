/* UART owner. init publishes a tty connection only after its late binding. */
#include "start.h"
#include "control.h"
#include "private/start.h"
#include "private/syscall.h"
#include <proto/init.h>
#include <abi/time.h>
#include <unistd.h>

#define MMIO_ADDRESS UINT64_C(0x2000000)
#define RX_ADDRESS   UINT64_C(0x2100000)
#define TX_ADDRESS   UINT64_C(0x2200000)
#define READY_NS     UINT64_C(10000000000)
#define RETRY_NS     UINT64_C(1000000)
#define EVENTS                                                                                     \
  (EVENT_TIMER | EVENT_RQ_DONE | EVENT_RQ_CANCEL | SERIAL_IRQ_EVENT | SERIAL_RX_ROOM |             \
   SERIAL_TX_READY)

typedef struct Serialstate Serialstate;
struct Serialstate
{
  u64 phase, pid, image, initimage, number, irq_events, received, transmitted;
  u64 peer, peerimage, error, stage, steps, ready, reserved[2];
};
/* Private debugger/diagnostic state, never a source of authority. */
volatile Serialstate serialstate;
static Serialboot    boot;
static Serialctl     control;
static Serialio      io;
static Uart          uart;
static Handle        ready;

static NORETURN void
failed(u64 stage, isize error)
{
  if (uart.initialized)
    (void) uartmask(&uart, 0, 0);
  if (io.rx)
    __atomic_fetch_or(&io.rx->flags, SERIAL_ERROR, __ATOMIC_RELEASE);
  if (control.peer)
    (void) ipc_notify(control.peer, SERIAL_RX_READY | SERIAL_TX_ROOM);
  if (ready)
    (void) ipc_rqcancel(ready);
  serialstate.error = (u64) error;
  serialstate.stage = stage;
  serialstate.phase = 5;
  _exit((int) stage);
}

static u64
now(void)
{
  int64_t seconds;
  u64     nanos;
  isize   error = __sys_clock_gettime(CLOCK_MONOTONIC, &seconds, &nanos);
  if (error || seconds < 0 || nanos >= UINT64_C(1000000000) ||
      (u64) seconds > (UINT64_MAX - nanos) / UINT64_C(1000000000))
    failed(101, error ? error : -ERR_OVERFLOW);
  return (u64) seconds * UINT64_C(1000000000) + nanos;
}

static void
alarm(u64 deadline)
{
  u64   generation;
  isize error = __sys_alarmset(deadline, &generation);
  if (error)
    failed(102, error);
}

static void
mapping(u32 slot, u64 address, u64 bytes)
{
  Maparg args;
  isize  error;
  args.mem = boot.handle[slot];
  args.offset = 0;
  args.address = address;
  args.length = bytes;
  args.prot = MAP_READ | MAP_WRITE;
  args.reserved = 0;
  error = __sys_map(boot.handle[SerialSpace], &args);
  if (error)
    failed(103, error);
}

static void
command(const Ipcmsg *message, u32 online)
{
  Msg    reply;
  Handle previous = control.peer;
  u64    state;
  isize  error = ipc_rqcheck(message->request, &state);
  if (error)
    failed(104, error);
  if (state == RQ_CHECK_DRAINING) {
    error = ipc_rqfinish(message->request);
  } else {
    serialcontrol(&control, &io, message, online, (u32) (state & RQ_CHECK_CANCEL), &reply);
    error = ipc_reply(message->request, &reply);
    if (!previous && control.peer) {
      serialstate.peer = (u64) control.peerpid;
      serialstate.peerimage = control.peerimage;
      serialstate.phase = 4;
    }
  }
  if (error)
    failed(105, error);
}

static void
readyresult(u64 deadline)
{
  Ipcmsg reply;
  u64    generation;
  isize  error;
  if (!ready)
    return;
  error = ipc_rqpoll(ready, 0, &reply);
  if (error == -ERR_WOULD_BLOCK) {
    if (now() >= deadline)
      failed(106, -ERR_TIMEDOUT);
    return;
  }
  if (error || reply.kind || reply.source || reply.request || reply.image || reply.words[0] ||
      reply.words[1] != 1 || reply.words[2] != boot.number || reply.words[3] != boot.image)
    failed(107, error ? error : -ERR_PEER);
  error = ipc_rqack(ready, ACK_RESULT | ACK_RELEASE);
  if (error)
    failed(108, error);
  ready = 0;
  error = __sys_alarmcancel(&generation);
  if (error)
    failed(109, error);
  serialstate.ready = 1;
  serialstate.phase = 3;
}

int
main(int argc, char **argv)
{
  Ipcmsg message;
  Msg    report;
  Handle session;
  u64    deadline, current, hints, pending = 0;
  u32    again, acknowledge = 0;
  isize  error;
  (void) argv;
  serialstate.phase = 1;
  error = argc == 7 ? serialboot(sysboot(), &boot) : -ERR_INVALID;
  if (!error)
    error = serialcheck(&boot);
  if (!error)
    error = serialctlinit(&control, boot.pid, boot.image, boot.initimage);
  if (error)
    failed(110, error);
  serialstate.pid = (u64) boot.pid;
  serialstate.image = boot.image;
  serialstate.initimage = boot.initimage;
  serialstate.number = boot.number;
  error = __sys_setsid(&session);
  if (error)
    failed(111, error);
  current = now();
  if (current > UINT64_MAX - READY_NS)
    failed(112, -ERR_OVERFLOW);
  deadline = current + READY_NS;
  if (boot.mapbytes > RX_ADDRESS - MMIO_ADDRESS)
    failed(113, -ERR_LIMIT);
  mapping(SerialMmio, MMIO_ADDRESS, boot.mapbytes);
  mapping(SerialRx, RX_ADDRESS, SERIAL_RING_BYTES);
  mapping(SerialTx, TX_ADDRESS, SERIAL_RING_BYTES);
  for (;;) {
    error = uartinit(&uart, MMIO_ADDRESS + boot.offset, boot.bytes, (u32) boot.kind,
                     (u32) boot.shift, (u32) boot.width);
    if (!error)
      break;
    if (error != -ERR_BUSY)
      failed(114, error);
    current = now();
    if (current >= deadline)
      failed(115, -ERR_TIMEDOUT);
    alarm(current + MIN(RETRY_NS, deadline - current));
    error = ipc_recv(EVENT_TIMER | EVENT_RQ_CANCEL, 0, &message);
    if (error == -ERR_INTERRUPTED)
      continue;
    if (error)
      failed(116, error);
    if (message.kind == IPC_CALL)
      command(&message, 0);
  }
  error = serialioinit(&io, &uart, (Serialring *) RX_ADDRESS, (Serialring *) TX_ADDRESS, 1);
  if (error)
    failed(117, error);
  error = serialiostep(&io, &hints, &again);
  if (error)
    failed(118, error);
  pending |= hints;
  error = __sys_irqack(boot.handle[SerialIrq]);
  if (error)
    failed(119, error);
  report.words[0] = INIT_WORD(INIT_READY);
  report.words[1] = 1;
  report.words[2] = boot.number;
  report.words[3] = boot.image;
  error = ipc_call(boot.handle[SerialInit], &report, CALL_ASYNC | CALL_CONTROL, &message);
  if (error || !message.request)
    failed(120, error ? error : -ERR_STATE);
  ready = message.request;
  serialstate.phase = 2;
  alarm(deadline);
  for (;;) {
    readyresult(deadline);
    error = serialiostep(&io, &hints, &again);
    if (error)
      failed(121, error);
    pending |= hints;
    if (serialstate.steps != UINT64_MAX)
      serialstate.steps++;
    serialstate.received = uart.received;
    serialstate.transmitted = uart.transmitted;
    if (acknowledge && !again) {
      error = __sys_irqack(boot.handle[SerialIrq]);
      if (error)
        failed(122, error);
      acknowledge = 0;
    }
    if (control.peer && pending) {
      error = ipc_notify(control.peer, pending);
      if (error)
        failed(123, error);
      pending = 0;
    }
    error = ipc_recv(EVENTS, again ? RECV_NONBLOCK : 0, &message);
    if (error && error != -ERR_WOULD_BLOCK && error != -ERR_INTERRUPTED)
      failed(124, error);
    if (!error && message.kind == IPC_CALL) {
      readyresult(deadline);
      command(&message, ready == 0);
    } else if (!error && message.kind == IPC_EVENT) {
      if (message.words[0] & SERIAL_IRQ_EVENT) {
        acknowledge = 1;
        if (serialstate.irq_events != UINT64_MAX)
          serialstate.irq_events++;
      }
    }
    if (again || error == -ERR_WOULD_BLOCK) {
      error = __sys_yield();
      if (error)
        failed(125, error);
    }
  }
}
