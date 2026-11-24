#include "service.h"
#include "private/argument.h"
#include "private/elf.h"
#include "private/serial.h"
#include "private/syscall.h"
#include <proto/init.h>
#include <proto/tty.h>
#include <abi/resource.h>
#include <abi/system.h>
#include <abi/observe.h>
#include <abi/time.h>
#include <string.h>

typedef struct Packet Packet;
typedef struct Work   Work;
typedef struct Uart   Uart;
struct Uart
{
  Handle mmio, irq;
  u64    kind, offset, bytes, shift, width, mapbytes;
};
struct Packet
{
  Boot     header;
  Resentry resources[7];
  u64      arguments[25];
};
STATIC_ASSERT(sizeof(Packet) == 568, serial_startup_packet);
struct Work
{
  const Bootitem *item;
  Elfsource       source;
  Elfplan         plan;
  Elfwork         load;
  Config          config;
  Packet          packet;
  Uart            uart;
  Handle          creator;
  u64             wake;
  u64             observation;
  Msg             request, reply;
  Handle          bindrpc, bindcall, bindnotify;
  u32             bindcancelled, bindwait;
};
enum
{
  StageImage = 1,
  StageHandoff,
  StageCreate,
  StageResources,
  StageLoad,
  StageStart,
  StageReady,
  StageRunning,
  StageBind
};
#define READY_NS UINT64_C(10000000000)
#define RETRY_NS UINT64_C(1000000)
#define MAP_RX   1
#define MAP_TX   2
#define STARTED  4
#define REAPED   8
#define KILLED   16

Servicestate           servicestate[INITCFG_SERVICES];
Queryrec               serviceaccounts[INITCFG_SERVICES], initaccount;
static Work            operations[INITCFG_SERVICES];
static const Manifest *policy;
static const Package  *catalog;
static Handle          space, allocator, system, listener, observer;
static u64             ownimage;
static u32             serialindex;

static isize
account(Handle control, Queryrec *out)
{
  u64   written, next;
  isize error = __sys_query(control, QUERY_ACCT, 0, out, 1, &written, &next);
  return error ? error : written == 1 && !next ? 0 : -ERR_STATE;
}

static isize
map(Handle memory, u64 address, u64 bytes)
{
  Maparg arg;
  arg.mem = memory;
  arg.offset = 0;
  arg.address = address;
  arg.length = bytes;
  arg.prot = MAP_READ | MAP_WRITE;
  arg.reserved = 0;
  return __sys_map(space, &arg);
}

static isize
readimage(void *context, u64 offset, void *out, u64 bytes)
{
  Work *work = context;
  if (!work->item || offset > work->item->elf_bytes || bytes > work->item->elf_bytes - offset)
    return -ERR_INVALID;
  memcpy(out, catalog->data + work->item->elf_off + offset, bytes);
  return 0;
}

static const char *
number(char text[21], u64 value)
{
  u32 position = 20;
  text[position] = 0;
  do {
    text[--position] = (char) ('0' + value % 10);
    value /= 10;
  } while (value);
  return text + position;
}

static isize
arguments(Packet *packet, u32 index)
{
  const Uart *uart = &operations[index].uart;
  u64         data[25], mask;
  char        numbers[5][21];
  const char *argv[8];
  Argsnap     snapshot;
  isize       error, restore;
  argv[0] = (const char *) policy->service[index].image;
  switch (policy->service[index].kind) {
  case INITCFG_SERIAL:
    argv[1] = uart->kind == BOOT_UART_NS16550 ? "ns16550" : "pl011";
    argv[2] = number(numbers[0], uart->offset);
    argv[3] = number(numbers[1], uart->bytes);
    argv[4] = number(numbers[2], servicestate[index].number);
    argv[5] = number(numbers[3], uart->shift);
    argv[6] = number(numbers[4], uart->width);
    argv[7] = 0;
    break;
  case INITCFG_TTY:
    argv[1] = number(numbers[0], servicestate[index].number);
    argv[2] = 0;
    break;
  default:
    return -ERR_NOTSUP;
  }
  error = __sys_sigmask(SIGNAL_BLOCK, SIGNAL_SUPPORTED, &mask);
  if (error)
    return error;
  error = sysargcopy(data, sizeof(data), argv, 0, &snapshot);
  if (!error)
    error = sysargboot(&snapshot, &packet->header, sizeof(*packet));
  restore = __sys_sigmask(SIGNAL_SETMASK, mask, &mask);
  return restore ? restore : error;
}

static void
stop(u32 index, isize error)
{
  Servicestate *state = &servicestate[index];
  state->state = SERVICE_STOPPING;
  if (!state->error)
    state->error = error;
  state->deadline = 0;
  operations[index].wake = 0;
}

static isize
cleanup(u32 index, u64 now)
{
  Servicestate *state = &servicestate[index];
  Work         *work = &operations[index];
  Queryrec      row;
  u64           written, next, address = SERVICE_RING_LO + (u64) index * SERVICE_STRIDE;
  u32           i;
  isize         error;
  work->bindwait = 0;
  if (work->bindrpc && !work->bindcancelled) {
    error = ipc_rqcancel(work->bindrpc);
    if (error)
      return error;
    work->bindcancelled = 1;
  }
  if (state->view) {
    if (work->observation) {
      error = __sys_obsdrop(work->observation);
      if (error)
        return error;
      work->observation = 0;
    }
    error = elfclean(&work->load);
    if (!error)
      error = __sys_destroy(state->view);
    if (error)
      return error;
    state->view = 0;
    state->flags |= REAPED;
  }
  if (state->pid && state->flags & STARTED && !(state->flags & KILLED)) {
    error = __sys_terminate(state->control);
    if (error)
      return error;
    state->flags |= KILLED;
  }
  if (work->observation) {
    u64    status, reason, image;
    Handle pid;
    error = __sys_obsread(work->observation, &status, &reason, &pid, &image);
    if (error || status != OBS_ENDED || pid != state->pid || image != state->image)
      return error ? error : -ERR_STATE;
    error = __sys_obsack(work->observation);
    if (error)
      return error;
    work->observation = 0;
  }
  if (state->pid && !(state->flags & REAPED)) {
    work->wake = now > UINT64_MAX - RETRY_NS ? UINT64_MAX : now + RETRY_NS;
    return 0;
  }
  if (state->control) {
    error = __sys_query(state->control, QUERY_PROC, 0, &row, 1, &written, &next);
    if (error || next)
      return error ? error : -ERR_STATE;
    if (written) {
      if (written != 1 || row.pid != state->pid || row.data[3] != PROCESS_EXITING)
        return -ERR_STATE;
      work->wake = now > UINT64_MAX - RETRY_NS ? UINT64_MAX : now + RETRY_NS;
      return 0;
    }
  }
  if (work->bindrpc) {
    Ipcmsg result;
    error = ipc_rqpoll(work->bindrpc, RQ_FINISH, &result);
    if (error == -ERR_WOULD_BLOCK) {
      work->wake = 0;
      work->bindwait = 1;
      return 0;
    }
    if (!error)
      error = ipc_rqack(work->bindrpc, ACK_RESULT | ACK_RELEASE);
    if (error)
      return error;
    work->bindrpc = 0;
  }
  if (work->bindcall) {
    error = __sys_free(work->bindcall);
    if (error)
      return error;
    work->bindcall = 0;
  }
  for (i = 0; i < 2; i++) {
    if (state->flags & (MAP_RX << i)) {
      Serialring *ring = (Serialring *) (address + (u64) i * SERVICE_TX_OFF);
      __atomic_fetch_or(&ring->flags, SERIAL_ERROR, __ATOMIC_RELEASE);
      error = __sys_unmap(space, (u64) ring);
      if (error)
        return error;
      state->flags &= ~(u64) (MAP_RX << i);
    }
    if (state->rings[i]) {
      error = __sys_free(state->rings[i]);
      if (error)
        return error;
      state->rings[i] = 0;
    }
  }
  if (state->control) {
    /* The exact old serial authority must outlive an unfinished BIND. */
    if (index == serialindex) {
      for (i = 0; i < policy->count; i++)
        if (operations[i].bindrpc) {
          work->wake = 0;
          work->bindwait = 1;
          return 0;
        }
    }
    error = __sys_free(state->control);
    if (error)
      return error;
    state->control = 0;
  }
  state->reclaimed = 1;
  state->state = SERVICE_FAILED;
  memset(&serviceaccounts[index], 0, sizeof(serviceaccounts[index]));
  work->wake = 0;
  return 0;
}

static isize
serialresources(u32 index, Handle handles[5])
{
  const Uart   *uart = &operations[index].uart;
  Servicestate *state = &servicestate[index];
  Allocarg      alloc;
  u64           address = SERVICE_RING_LO + (u64) index * SERVICE_STRIDE;
  u32           i;
  isize         error;
  memset(&alloc, 0, sizeof(alloc));
  alloc.bytes = SERIAL_RING_BYTES;
  alloc.kind = ALLOC_SHARED;
  for (i = 0; i < 2; i++) {
    error = __sys_alloc(allocator, &alloc, &state->rings[i]);
    if (!error)
      error = map(state->rings[i], address + (u64) i * SERVICE_TX_OFF, SERIAL_RING_BYTES);
    if (error)
      return error;
    state->flags |= MAP_RX << i;
    error = serialinit((Serialring *) (address + (u64) i * SERVICE_TX_OFF), 1);
    if (error)
      return error;
  }
  error = __sys_grant(state->view, listener, RESOURCE_USE, 0, 0, 0, &handles[0]);
  if (!error)
    error = __sys_grant(state->view, uart->mmio, MAP_READ | MAP_WRITE, 0, uart->mapbytes, 0,
                        &handles[1]);
  if (!error)
    error = __sys_irqbind(state->control, uart->irq, SERIAL_IRQ_BIT, &handles[2]);
  for (i = 0; !error && i < 2; i++)
    error = __sys_grant(state->view, state->rings[i], MAP_READ | MAP_WRITE, 0, SERIAL_RING_BYTES, 0,
                        &handles[3 + i]);
  return error;
}

static isize
ttyresources(Handle view, Handle handles[5])
{
  const Servicestate *serial = &servicestate[serialindex];
  Handle              notify;
  isize               error, release;
  u32                 i;
  error = __sys_grant(view, listener, RESOURCE_USE, 0, 0, 0, &handles[0]);
  for (i = 0; !error && i < 2; i++)
    error = __sys_grant(view, serial->rings[i], MAP_READ | MAP_WRITE, 0, SERIAL_RING_BYTES, 0,
                        &handles[1 + i]);
  if (!error)
    error = __sys_connect(serial->control, CONNECT_NOTIFY, RESOURCE_USE | RESOURCE_GRANT,
                          SERIAL_RX_ROOM | SERIAL_TX_READY, &notify);
  if (error)
    return error;
  error = __sys_grant(view, notify, RESOURCE_USE, 0, 0, 0, &handles[3]);
  release = __sys_free(notify);
  return error ? error : release;
}

static isize
resources(u32 index)
{
  Packet          *packet = &operations[index].packet;
  Handle           handles[5];
  static const u64 ttytags[] = {TTY_INIT_CALL, TTY_RX_RING, TTY_TX_RING, TTY_SERIAL_NOTIFY};
  u32              tty = policy->service[index].kind == INITCFG_TTY;
  u32              i, count = tty ? ARRAY_SIZE(ttytags) : ARRAY_SIZE(handles);
  isize            error =
      tty ? ttyresources(servicestate[index].view, handles) : serialresources(index, handles);
  if (error)
    return error;
  memset(packet, 0, sizeof(*packet));
  packet->header.magic = BOOT_MAGIC;
  packet->header.format = BOOT_FORMAT;
  packet->header.bytes = offsetof(Packet, resources) + (count + 2) * sizeof(Resentry);
  packet->header.resource_count = count + 2;
  packet->header.resources_off = offsetof(Packet, resources);
  packet->header.user_lo = BOOT_USER_LO;
  packet->header.user_hi = BOOT_USER_HI;
  packet->resources[0].tag = BOOT_SELF_SPACE;
  packet->resources[1].tag = BOOT_SELF_ALLOC;
  for (i = 0; i < count; i++) {
    packet->resources[2 + i].tag = tty ? ttytags[i] : SERIAL_INIT_CALL + i;
    packet->resources[2 + i].handle = handles[i];
  }
  return arguments(packet, index);
}

static isize
bindtty(u32 index, u64 now)
{
  Servicestate *tty = &servicestate[index], *serial = &servicestate[serialindex];
  Work         *work = &operations[index];
  Ipcmsg        result;
  Queryrec      row;
  u64           written, next;
  Handle        notify;
  isize         error, release;
  Msg           request;
  if (now >= tty->deadline)
    return -ERR_TIMEDOUT;
  if (!work->bindrpc) {
    error = __sys_connect(tty->control, CONNECT_NOTIFY, RESOURCE_USE | RESOURCE_GRANT,
                          SERIAL_RX_READY | SERIAL_TX_ROOM, &notify);
    if (error)
      return error;
    error = __sys_grant(serial->control, notify, RESOURCE_USE, 0, 0, 0, &work->bindnotify);
    release = __sys_free(notify);
    if (error || release)
      return error ? error : release;
    error = __sys_connect(serial->control, CONNECT_CALL, RESOURCE_USE, 0, &work->bindcall);
    if (error)
      return error;
    request = (Msg){{SERIAL_WORD(SERIAL_BIND), 1, (u64) work->bindnotify, tty->image}};
    error = ipc_call(work->bindcall, &request, CALL_ASYNC | CALL_CONTROL, &result);
    work->bindrpc = result.request;
    return error ? error : work->bindrpc ? 0 : -ERR_STATE;
  }
  error = ipc_rqpoll(work->bindrpc, RQ_FINISH, &result);
  if (error == -ERR_WOULD_BLOCK)
    return 0;
  if (error)
    return error;
  error = ipc_rqpoll(work->bindrpc, 0, &result);
  if (!error &&
      (result.kind || result.source || result.request || result.image || result.words[1] != 1))
    error = -ERR_STATE;
  if (!error &&
      ((isize) result.words[0] > 0 || (result.words[0] && (result.words[2] || result.words[3]))))
    error = -ERR_STATE;
  if (!error)
    error = (isize) result.words[0];
  if (!error && (result.words[2] != (u64) tty->pid || result.words[3] != tty->image))
    error = -ERR_PEER;
  release = ipc_rqack(work->bindrpc, ACK_RESULT | ACK_RELEASE);
  if (release)
    return release;
  work->bindrpc = 0;
  release = __sys_free(work->bindcall);
  if (release)
    return release;
  work->bindcall = 0;
  if (!error) {
    error = __sys_query(tty->control, QUERY_PROC, 0, &row, 1, &written, &next);
    if (!error && (written != 1 || next || row.pid != tty->pid || row.image != tty->image ||
                   row.data[3] == PROCESS_EXITING))
      error = -ERR_PEER;
  }
  if (error) {
    release = __sys_revoke(serial->control, work->bindnotify);
    if (release && release != -ERR_STALE)
      return release;
    return error;
  }
  tty->state = SERVICE_READY;
  tty->stage = StageRunning;
  tty->deadline = 0;
  return 0;
}

isize
serviceinit(const Manifest *manifest, const Package *package, const Bootview *boot, u64 image)
{
  Queryrec rows[4];
  u64      cursor = 0, written, next, i, j;
  u32      batches = 0;
  Handle   pid, parent;
  isize    error;
  Uart    *uart = NULL;
  if (!manifest || !package || !boot || !image)
    return -ERR_INVALID;
  if (policy)
    return -ERR_STATE;
  error = __sys_getid(&pid, &ownimage, &parent);
  if (error || pid != 1 || parent || ownimage != image)
    return error ? error : -ERR_DENIED;
  space = bootresource(boot, BOOT_SELF_SPACE);
  allocator = bootresource(boot, BOOT_SELF_ALLOC);
  system = bootresource(boot, BOOT_SYSTEM);
  if (!space || !allocator || !system)
    return -ERR_DENIED;
  for (i = 0; i < manifest->count; i++) {
    operations[i].creator = bootresource(boot, manifest->service[i].creator);
    if (!operations[i].creator)
      return -ERR_DENIED;
  }
  do {
    if (batches++ == BOOT_RESOURCE_COUNT)
      return -ERR_LIMIT;
    error = __sys_query(0, QUERY_RESOURCE, cursor, rows, ARRAY_SIZE(rows), &written, &next);
    if (error)
      return error;
    if (written > ARRAY_SIZE(rows) || (next && (!written || next == cursor)))
      return -ERR_STATE;
    for (i = 0; i < written; i++) {
      if (rows[i].type != QUERY_RESOURCE || rows[i].pid != 1 || rows[i].image != image)
        return -ERR_STATE;
      if (rows[i].data[1] == RESOURCE_CALL && rows[i].data[2] == (RESOURCE_USE | RESOURCE_GRANT) &&
          rows[i].data[6] == 1 && rows[i].data[7] == image) {
        if (listener)
          return -ERR_STATE;
        listener = (Handle) rows[i].data[0];
      }
      if (rows[i].data[1] == RESOURCE_OBSERVE && rows[i].data[2] == OBSERVE_MONITOR &&
          !rows[i].data[5]) {
        if (observer)
          return -ERR_STATE;
        observer = (Handle) rows[i].data[0];
      }
    }
    cursor = next;
  } while (cursor);
  if (!listener || !observer)
    return -ERR_DENIED;
  for (i = 0; i < manifest->count; i++) {
    if (manifest->service[i].kind != INITCFG_SERIAL)
      continue;
    if (uart)
      return -ERR_INVALID;
    serialindex = (u32) i;
    uart = &operations[i].uart;
  }
  if (!uart)
    return -ERR_INVALID;
  error = __sys_query(system, QUERY_BOOT_RESOURCE, 0, rows, 1, &written, &next);
  if (error || written != 1 || next)
    return error ? error : -ERR_STATE;
  uart->mmio = (Handle) rows[0].data[0];
  uart->irq = (Handle) rows[0].data[4];
  uart->kind = rows[0].data[1];
  uart->offset = rows[0].data[2] & PAGE_MASK;
  uart->bytes = rows[0].data[3];
  uart->shift = rows[0].data[6];
  uart->width = rows[0].data[7];
  if (rows[0].type != QUERY_BOOT_RESOURCE || rows[0].pid != 1 || rows[0].image != image ||
      !uart->mmio || !uart->irq || rows[0].data[5] ||
      (uart->kind != BOOT_UART_NS16550 && uart->kind != BOOT_UART_PL011) || uart->shift > 4 ||
      (uart->width != 1 && uart->width != 4) || (uart->offset & (uart->width - 1)) ||
      (uart->kind == BOOT_UART_NS16550 && ((UINT64_C(1) << uart->shift) & (uart->width - 1))) ||
      (uart->kind == BOOT_UART_PL011 && (uart->shift || uart->width != 4)) ||
      uart->bytes <
          (uart->kind == BOOT_UART_PL011 ? 0x4c : (UINT64_C(5) << uart->shift) + uart->width) ||
      uart->bytes > UINT64_MAX - uart->offset ||
      uart->bytes + uart->offset > UINT64_MAX - PAGE_MASK)
    return -ERR_INVALID;
  for (j = 8; j < ARRAY_SIZE(rows[0].data); j++)
    if (rows[0].data[j])
      return -ERR_INVALID;
  uart->mapbytes = ALIGN_UP(uart->offset + uart->bytes, PAGE_SIZE);
  policy = manifest;
  catalog = package;
  for (i = 0; i < policy->count; i++) {
    servicestate[i].number = policy->service[i].number;
    servicestate[i].state = SERVICE_STARTING;
    servicestate[i].stage = StageImage;
  }
  return 0;
}

isize
servicestep(u64 now, u32 *work, u64 *next)
{
  u32   i, j;
  isize error;
  *work = 0;
  *next = 0;
  for (i = 0; i < policy->count; i++) {
    Servicestate     *state = &servicestate[i];
    Work             *operation = &operations[i];
    const Servicecfg *configuration = &policy->service[i];
    u32               tty = configuration->kind == INITCFG_TTY;
    error = 0;
    if (operation->observation && state->state != SERVICE_STOPPING) {
      u64    status, reason, image;
      Handle pid;
      error = __sys_obsread(operation->observation, &status, &reason, &pid, &image);
      if (error || pid != state->pid || image != state->image)
        return error ? error : -ERR_STATE;
      if (status == OBS_ENDED) {
        error = __sys_obsack(operation->observation);
        if (error)
          return error;
        operation->observation = 0;
        stop(i, reason == OBS_TIMEOUT ? -ERR_TIMEDOUT : -ERR_PEER);
      } else if (status != OBS_ACTIVE && status != OBS_PREPARING)
        return -ERR_STATE;
    }
    if (state->state == SERVICE_STOPPING) {
      Handle binding = operation->bindrpc;
      error = cleanup(i, now);
      if (error)
        return error;
      /* An earlier serial pass may still be holding CONTROL for this request. */
      if (binding && !operation->bindrpc)
        *work = 1;
    } else if (state->state == SERVICE_STARTING && operation->wake <= now &&
               (!tty || servicestate[serialindex].state == SERVICE_READY)) {
      switch (state->stage) {
      case StageImage:
        operation->item = packagefind(catalog, (const char *) configuration->image);
        if (!operation->item) {
          error = -ERR_INVALID;
          break;
        }
        operation->source.context = operation;
        operation->source.bytes = operation->item->elf_bytes;
        operation->source.read = readimage;
        error = elfcheck(&operation->source, configuration->memlimit, &operation->plan);
        if (!error) {
          if (now > UINT64_MAX - READY_NS)
            return -ERR_OVERFLOW;
          state->deadline = now + READY_NS;
          state->stage = tty ? StageCreate : StageHandoff;
          if (tty)
            state->deadline = 0;
        }
        break;
      case StageHandoff:
        error = __sys_debughandoff(system);
        if (error == -ERR_BUSY && now < state->deadline) {
          operation->wake = now + MIN(RETRY_NS, state->deadline - now);
          error = 0;
        } else if (error == -ERR_BUSY)
          error = -ERR_TIMEDOUT;
        else if (!error) {
          state->deadline = operation->wake = 0;
          state->stage = StageCreate;
        }
        break;
      case StageCreate: {
        Createarg args;
        memset(&args, 0, sizeof(args));
        args.memlimit = configuration->memlimit;
        args.exec_limit = configuration->exec_limit;
        args.resources = (u32) configuration->resources;
        args.pending = (u32) configuration->pending;
        error =
            __sys_create(operations[i].creator, &args, &state->view, &state->pid, &state->control);
        if (!error) {
          state->image = 1;
          operation->load.view = state->view;
          operation->load.pid = state->pid;
          operation->load.image = 1;
          operation->load.parent_space = space;
          operation->load.scratch = 0x2000000;
          operation->load.scratch_bytes = BOOT_MAX_BYTES;
          error = __sys_sigmask(SIGNAL_BLOCK, 0, &operation->load.sigmask);
          if (!error)
            state->stage = StageResources;
        }
        break;
      }
      case StageResources:
        error = resources(i);
        if (!error)
          state->stage = StageLoad;
        break;
      case StageLoad:
        error = elfload(&operation->source, &operation->plan, &operation->packet,
                        operation->packet.header.bytes, &operation->load, &operation->config);
        if (!error)
          error = __sys_configure(state->view, &operation->config, 0);
        if (!error)
          state->stage = StageStart;
        break;
      case StageStart: {
        u64     status, reason, nanos, started;
        int64_t seconds;
        error = __sys_observe(observer, state->control, OBS_MONITOR, state->pid, state->image,
                              &operation->observation, &status, &reason);
        if (error)
          break;
        if (status != OBS_PREPARING) {
          error = -ERR_STATE;
          break;
        }
        error = __sys_start(state->view);
        if (!error) {
          state->view = 0;
          state->flags |= STARTED;
          state->stage = StageReady;
          error = __sys_clock_gettime(CLOCK_MONOTONIC, &seconds, &nanos);
          if (error)
            return error;
          if (seconds < 0 || nanos >= UINT64_C(1000000000))
            return -ERR_STATE;
          if ((u64) seconds > (UINT64_MAX - nanos) / UINT64_C(1000000000))
            return -ERR_OVERFLOW;
          started = (u64) seconds * UINT64_C(1000000000) + nanos;
          if (started > UINT64_MAX - READY_NS)
            return -ERR_OVERFLOW;
          state->deadline = started + READY_NS;
        }
        break;
      }
      case StageReady:
        if (now >= state->deadline)
          error = -ERR_TIMEDOUT;
        break;
      case StageBind:
        error = bindtty(i, now);
        break;
      default:
        return -ERR_STATE;
      }
      if (error)
        stop(i, error);
    }
    if (state->state == SERVICE_STOPPING || state->state == SERVICE_FAILED)
      for (j = 0; j < policy->count; j++)
        if (servicestate[j].state == SERVICE_STARTING || servicestate[j].state == SERVICE_READY) {
          stop(j, -ERR_PEER);
          *work = 1;
        }
    if (state->state == SERVICE_STOPPING)
      *work |= !operation->bindwait && operation->wake <= now;
    if (state->state == SERVICE_STARTING) {
      if (!tty || servicestate[serialindex].state == SERVICE_READY)
        *work |= state->stage != StageReady && (state->stage != StageBind || !operation->bindrpc) &&
                 operation->wake <= now;
      if (state->deadline && (!*next || state->deadline < *next))
        *next = state->deadline;
    }
    if (operation->wake && (!*next || operation->wake < *next))
      *next = operation->wake;
    if (state->state == SERVICE_READY && state->control) {
      error = account(state->control, &serviceaccounts[i]);
      if (error == -ERR_STATE) {
        stop(i, -ERR_PEER);
        *work = 1;
      } else if (error)
        return error;
    }
  }
  return account(0, &initaccount);
}

void
serviceexit(Handle pid, u64 kind, u64 code)
{
  u32 i;
  for (i = 0; i < policy->count; i++)
    if (servicestate[i].pid == pid && !servicestate[i].reclaimed) {
      servicestate[i].exitkind = kind;
      servicestate[i].exitcode = code;
      servicestate[i].flags |= REAPED;
      stop(i, -ERR_PEER);
      return;
    }
}

isize
servicemessage(const Ipcmsg *message, u64 now)
{
  Servicestate *state = NULL;
  Work         *work = NULL;
  Msg           words;
  Queryrec      row;
  u64           requeststate, written, next;
  u32           i, j, opcode = (u32) message->words[0];
  isize         error, status = -ERR_DENIED;
  for (j = 0; j < 4; j++)
    words.words[j] = 0;
  words.words[1] = message->words[1];
  error = ipc_rqcheck(message->request, &requeststate);
  if (error)
    return error;
  for (i = 0; i < policy->count; i++)
    if (servicestate[i].pid == message->source && servicestate[i].image == message->image) {
      state = &servicestate[i];
      work = &operations[i];
      break;
    }
  if (requeststate == RQ_CHECK_DRAINING)
    return ipc_rqfinish(message->request);
  if (!state || message->words[2] != state->number || message->words[3] != state->image)
    goto reply;
  status = -ERR_NOTSUP;
  if (INIT_WORD_VERSION(message->words[0]) != INIT_VERSION ||
      (opcode != INIT_READY && opcode != INIT_STATUS))
    goto reply;
  status = -ERR_INVALID;
  if (!message->words[1])
    goto reply;
  status = -ERR_STALE;
  if (message->words[1] < state->sequence)
    goto reply;
  if (message->words[1] == state->sequence) {
    status = -ERR_STATE;
    for (j = 0; j < 4; j++)
      if (message->words[j] != work->request.words[j])
        goto reply;
    return ipc_reply(message->request, &work->reply);
  }
  status = -ERR_CANCELLED;
  if (requeststate & RQ_CHECK_CANCEL) {
    if (opcode == INIT_READY)
      stop(i, status);
    goto remember;
  }
  if (opcode == INIT_STATUS) {
    words.words[2] = state->state;
    words.words[3] = (u64) state->error;
    status = 0;
  } else if (state->state != SERVICE_STARTING || state->stage != StageReady) {
    status = -ERR_STATE;
  } else if (now >= state->deadline) {
    status = -ERR_TIMEDOUT;
    stop(i, status);
  } else {
    error = __sys_query(state->control, QUERY_PROC, 0, &row, 1, &written, &next);
    if (error)
      return error;
    if (written != 1 || next || row.pid != state->pid || row.image != state->image ||
        row.data[3] == PROCESS_EXITING) {
      status = -ERR_PEER;
      stop(i, status);
    } else {
      if (policy->service[i].kind == INITCFG_TTY) {
        state->stage = StageBind;
        state->deadline = now + MIN(READY_NS, UINT64_MAX - now);
      } else {
        state->state = SERVICE_READY;
        state->stage = StageRunning;
        state->deadline = 0;
      }
      words.words[2] = state->number;
      words.words[3] = state->image;
      status = 0;
    }
  }
remember:
  state->sequence = message->words[1];
  for (j = 0; j < 4; j++)
    work->request.words[j] = message->words[j];
  words.words[0] = (u64) status;
  work->reply = words;
reply:
  words.words[0] = (u64) status;
  return ipc_reply(message->request, &words);
}

u32
servicephase(void)
{
  u32 i, phase = 1;
  for (i = 0; i < policy->count; i++) {
    if (servicestate[i].state == SERVICE_STARTING || servicestate[i].state == SERVICE_STOPPING)
      return 0;
    if (servicestate[i].state == SERVICE_FAILED)
      phase = 3;
  }
  return phase;
}
