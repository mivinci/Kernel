#include "serial.h"
#include "start.h"
#include "private/start.h"
#include "private/syscall.h"
#include <abi/resource.h>
#include <abi/time.h>
#include <proto/init.h>
#include <sys/channel.h>
#include <unistd.h>

#define RX_ADDRESS UINT64_C(0x2100000)
#define TX_ADDRESS UINT64_C(0x2200000)
#define READY_NS   UINT64_C(10000000000)
#define EVENTS     (SERIAL_RX_READY | SERIAL_TX_ROOM | EVENT_TIMER | EVENT_RQ_DONE | EVENT_RQ_CANCEL)

enum
{
  TtySpace,
  TtyInit,
  TtyRx,
  TtyTx,
  TtyNotify,
  TtyHandles
};

static const u64 tags[TtyHandles] = {BOOT_SELF_SPACE, TTY_INIT_CALL, TTY_RX_RING, TTY_TX_RING,
                                     TTY_SERIAL_NOTIFY};
/* Before the terminal Local arrives, no foreground signal target is available.
   The UART retains its firmware-selected rate; no speed is reported here. */
static const Termarg mode = {
    TTY_ICRNL, TTY_OPOST | TTY_ONLCR,        TTY_CS8, TTY_ICANON | TTY_ECHO, 0,
    0,         {3, 28, 127, 21, 4, 0, 1, 26}};
static Ttyinput  input;
static Ttyserial transport;
static Handle    handles[TtyHandles], ready;

typedef struct Ttystate Ttystate;
struct Ttystate
{
  u64 phase, pid, image, number, error, stage, received, dropped;
};
/* Private debugger state; authority remains in the resource table. */
volatile Ttystate ttystate;

static NORETURN void
failed(u32 stage, isize error)
{
  if (ready)
    (void) ipc_rqcancel(ready);
  ttystate.error = (u64) error;
  ttystate.stage = stage;
  ttystate.phase = 4;
  _exit((int) stage);
}

static isize
ttyboot(const Bootview *boot, u64 *number)
{
  const char *text;
  u64         i, j, k, value = 0;
  u32         digit;
  if (!boot || !boot->header || boot->header->argc != 2 || boot->argv[0].length < 2)
    return -ERR_INVALID;
  text = bootarg(boot, 1, 0);
  for (i = 0; text[i]; i++) {
    if (text[i] < '0' || text[i] > '9')
      return -ERR_INVALID;
    digit = (u32) (text[i] - '0');
    if (value > (UINT64_MAX - digit) / 10)
      return -ERR_OVERFLOW;
    value = value * 10 + digit;
  }
  if (!value)
    return -ERR_INVALID;
  for (i = 0; i < boot->header->resource_count; i++) {
    const Resentry *entry = &boot->resources[i];
    for (j = 0; j < TtyHandles; j++) {
      if (entry->tag != tags[j])
        continue;
      if (handles[j] || entry->flags)
        return -ERR_INVALID;
      for (k = 0; k < TtyHandles; k++)
        if (handles[k] == entry->handle)
          return -ERR_INVALID;
      handles[j] = entry->handle;
      break;
    }
  }
  for (j = 0; j < TtyHandles; j++)
    if (!handles[j])
      return -ERR_INVALID;
  *number = value;
  return 0;
}

static int
resource(u32 slot, const Queryrec *row, Handle pid)
{
  const u64 *data = row->data;
  if (data[5])
    return 0;
  if (slot == TtyRx || slot == TtyTx)
    return data[1] == RESOURCE_MEM && data[2] == (MAP_READ | MAP_WRITE) && !(data[3] & PAGE_MASK) &&
           data[4] == SERIAL_RING_BYTES && !data[6] && !data[7];
  if (data[3] || data[4])
    return 0;
  if (slot == TtySpace)
    return data[1] == RESOURCE_SPACE && data[2] == 3 && !data[6] && !data[7];
  if (data[2] != RESOURCE_USE || !data[7])
    return 0;
  if (slot == TtyInit)
    return data[1] == RESOURCE_CALL && data[6] == 1;
  return data[1] == RESOURCE_NOTIFY && handlekind((Handle) data[6]) == HANDLE_PROCESS &&
         data[6] != 1 && data[6] != (u64) pid;
}

static isize
inventory(Handle pid, u64 image)
{
  Queryrec rows[4];
  u64      cursor = 0, next, written, i, seen = 0;
  u32      batch, slot;
  isize    error;
  for (batch = 0; batch < BOOT_RESOURCE_COUNT; batch++) {
    error = __sys_query(0, QUERY_RESOURCE, cursor, rows, ARRAY_SIZE(rows), &written, &next);
    if (error)
      return error;
    if (written > ARRAY_SIZE(rows) || (next && (!written || next == cursor)))
      return -ERR_STATE;
    for (i = 0; i < written; i++) {
      if (rows[i].type != QUERY_RESOURCE || rows[i].pid != pid || rows[i].image != image)
        return -ERR_STATE;
      for (slot = 0; slot < TtyHandles; slot++) {
        if (rows[i].data[0] != (u64) handles[slot])
          continue;
        if (!resource(slot, &rows[i], pid))
          return -ERR_DENIED;
        seen |= UINT64_C(1) << slot;
      }
    }
    if (seen == (UINT64_C(1) << TtyHandles) - 1)
      return 0;
    if (!next)
      return -ERR_DENIED;
    cursor = next;
  }
  return -ERR_LIMIT;
}

static isize
mapping(Handle memory, u64 address)
{
  Maparg args = {memory, 0, address, SERIAL_RING_BYTES, MAP_READ | MAP_WRITE, 0};
  return __sys_map(handles[TtySpace], &args);
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
readyresult(u64 deadline, u64 number, u64 image)
{
  Ipcmsg reply;
  u64    generation;
  isize  error = ipc_rqpoll(ready, 0, &reply);
  if (error == -ERR_WOULD_BLOCK) {
    if (now() >= deadline)
      failed(106, -ERR_TIMEDOUT);
    return;
  }
  if (error || reply.kind || reply.source || reply.request || reply.image || reply.words[0] ||
      reply.words[1] != 1 || reply.words[2] != number || reply.words[3] != image)
    failed(105, error ? error : -ERR_PEER);
  error = ipc_rqack(ready, ACK_RESULT | ACK_RELEASE);
  if (error)
    failed(105, error);
  ready = 0;
  error = __sys_alarmcancel(&generation);
  if (error)
    failed(105, error);
  ttystate.phase = 3;
}

static void
reject(const Ipcmsg *message)
{
  Msg   reply = {{(u64) -ERR_NOTSUP, message->words[1], 0, 0}};
  u64   state;
  isize error = ipc_rqcheck(message->request, &state);
  if (!error) {
    if (state == RQ_CHECK_DRAINING)
      error = ipc_rqfinish(message->request);
    else {
      if (state & RQ_CHECK_CANCEL)
        reply.words[0] = (u64) -ERR_CANCELLED;
      error = ipc_reply(message->request, &reply);
    }
  }
  if (error)
    failed(108, error);
}

int
main(int argc, char **argv)
{
  Handle          pid, parent, session;
  Ipcmsg          message;
  Msg             report;
  Ttyserialresult result;
  u64             number, image, deadline, generation;
  isize           error;
  (void) argc;
  (void) argv;
  ttystate.phase = 1;
  error = ttyboot(sysboot(), &number);
  if (!error)
    error = __sys_getid(&pid, &image, &parent);
  if (!error && (parent != 1 || pid == 1 || handlekind(pid) != HANDLE_PROCESS || !image))
    error = -ERR_DENIED;
  if (!error)
    error = inventory(pid, image);
  if (!error)
    error = __sys_setsid(&session);
  if (error)
    failed(102, error);
  ttystate.pid = (u64) pid;
  ttystate.image = image;
  ttystate.number = number;
  error = mapping(handles[TtyRx], RX_ADDRESS);
  if (!error)
    error = mapping(handles[TtyTx], TX_ADDRESS);
  if (!error)
    error = ttyinputinit(&input, 1);
  if (!error)
    error = ttyserialinit(&transport, (Serialring *) RX_ADDRESS, (Serialring *) TX_ADDRESS, 1);
  if (error)
    failed(103, error);
  deadline = now();
  if (deadline > UINT64_MAX - READY_NS)
    failed(104, -ERR_OVERFLOW);
  deadline += READY_NS;
  error = __sys_alarmset(deadline, &generation);
  if (error)
    failed(104, error);
  report = (Msg){{INIT_WORD(INIT_READY), 1, number, image}};
  error = ipc_call(handles[TtyInit], &report, CALL_ASYNC | CALL_CONTROL, &message);
  if (error || !message.request)
    failed(104, error ? error : -ERR_STATE);
  ready = message.request;
  ttystate.phase = 2;
  for (;;) {
    u32 again = 0;
    if (ready)
      readyresult(deadline, number, image);
    if (!ready) {
      error = ttyserialpump(&transport, &input, &mode, SERIAL_BATCH, handles[TtyNotify], &result);
      if (error || result.end)
        failed(107, error ? error : -ERR_PEER);
      ttystate.received = __atomic_load_n(&transport.rx->consumer, __ATOMIC_RELAXED);
      ttystate.dropped = input.dropped;
      again = result.again;
    }
    error = ipc_recv(EVENTS, again ? RECV_NONBLOCK : 0, &message);
    if (error && error != -ERR_WOULD_BLOCK && error != -ERR_INTERRUPTED)
      failed(108, error);
    if (!error && message.kind == IPC_CALL)
      reject(&message);
    if (again) {
      error = __sys_yield();
      if (error)
        failed(108, error);
    }
  }
}
