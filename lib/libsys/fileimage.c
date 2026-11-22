#include "private/fileimage.h"
#include "private/argument.h"
#include "private/close.h"
#include "private/sigentry.h"
#include "private/start.h"
#include "private/syscall.h"
#include "private/timer.h"
#include <abi/signal.h>
#include <errno.h>
#include <proto/file.h>
#include <proto/tty.h>

static Fileimage image;
static Conntag   fstag;
static Rkey      hello;
static Bootmap   candidate;
static u32       initialized, connecting, closing, draining;
static void      closeprogress(Fileimage *state);
static int       fileprogress(Fileimage *state);
static int       fileevents(void);

isize
sysfilepollsubmit(Connwork *work, Conncache *cache, const Opkey *key)
{
  Msg    message;
  Ipcmsg result;
  isize  error = sysoprequest(work, cache, key, &message);
  if (error)
    return error;
  if (message.words[0] != FS_POLL_QUERY && message.words[0] != FS_POLL_WAIT)
    return -ERR_INVALID;
  error = ipc_call(work->state.call, &message, CALL_ASYNC, &result);
  return error ? error : sysopsubmitted(work, cache, key, &result.request);
}

isize
sysfilepollcollect(Connwork *work, Conncache *cache, Opkey *key, Filepoll *rows, u32 count,
                   Fileioresult *result, u64 *pin)
{
  Slotop *op;
  isize   error;
  if (!key || !key->owner || !result || !pin || *pin || !rows || !count || count > FS_POLL_ROWS)
    return -ERR_INVALID;
  error = sysopcheck(work, cache, key);
  if (error)
    return error;
  op = &work->state.operations[key->lease.slot];
  if ((op->opcode != FS_POLL_QUERY && op->opcode != FS_POLL_WAIT) || op->object != count)
    return -ERR_INVALID;
  error = sysoppoll(work, cache, key);
  if (error)
    return error;
  if (op->status == -ERR_PEER && !(op->flags & SYSOP_BAD_RESULT) && !op->kind && !op->words[0] &&
      !op->words[1] && !op->words[2] && !op->words[3] && !work->state.ended) {
    error = sysoppeerended(work, cache, key);
    if (error)
      return error;
  }
  error = sysopfinished(work, cache, key);
  if (error)
    return error;
  if (!(op->flags & SYSOP_SAVED)) {
    result->native = result->business = 0;
    result->count = 0;
    if (op->kind != IPC_NONE || op->resource || op->flags & SYSOP_BAD_RESULT)
      result->native = -ERR_STATE;
    else if (op->status) {
      result->native =
          op->words[0] || op->words[1] || op->words[2] || op->words[3] ? -ERR_STATE : op->status;
    } else if (op->words[0]) {
      if (op->words[1] != key->lease.seq || (isize) op->words[0] > 0 || op->words[2] ||
          op->words[3])
        result->native = -ERR_STATE;
      else
        result->business = (isize) op->words[0];
    } else {
      error = sysfilepollresult((const void *) (uintptr_t) work->state.map.address,
                                work->state.map.bytes, key->lease, op->words, rows, count);
      if (error < 0)
        result->native = error;
      else
        result->count = (u64) error;
    }
    if (op->resource) {
      error = __sys_free(op->resource);
      if (error)
        return error;
      op->resource = 0;
      op->flags |= SYSOP_RESOURCE_MOVED;
    }
    error = sysopsaved(work, cache, key);
    if (error)
      return error;
  }
  if (op->phase != SYSOP_ACKED) {
    error = sysopack(work, cache, key);
    if (error)
      return error;
  }
  error = sysopfinish(work, cache, key, pin);
  if (!error)
    key->lease.seq = 0;
  return error;
}

isize
sysfilepollabandon(Fileimage *state, Opkey *key)
{
  Opkey background;
  u32   index = key->connection.index;
  isize error;
  if (index >= SYSCONN_COUNT)
    return -ERR_INVALID;
  error = sysoppollabandon(&state->connections[index], sysfileimagecache(state), key, &background);
  if (!error)
    draining |= 1U << index;
  return error;
}

isize
sysfilepollrelease(Fileimage *state, Pollfile *items, u32 count)
{
  isize error = 0;
  u32   i;
  for (i = 0; i < count; i++) {
    isize status;
    if (!items[i].file)
      continue;
    status = sysfilerefunpin(&state->files, state->files.self, items[i].file);
    if (!status)
      items[i].file = 0;
    else if (!error)
      error = status;
  }
  return error;
}

isize
sysfilepollcapture(Fileimage *state, const Pollfd *fds, Pollfile *items, u32 count)
{
  Fdview  view;
  Fileref ref;
  isize   error = 0;
  u32     i;
  if (!state || (count && (!fds || !items)))
    return -ERR_INVALID;
  for (i = 0; i < count; i++) {
    Pollfile *item = &items[i];
    item->fd = fds[i].fd;
    item->file = 0;
    item->connection.generation = 0;
    item->connection.index = item->connection.reserved = 0;
    item->row.session = item->row.fd_generation = item->row.revents = 0;
    item->row.events = fds[i].events & (FS_POLL_IN | FS_POLL_PRI | FS_POLL_OUT);
    if (item->fd < 0)
      continue;
    if ((u32) item->fd < BOOT_FD_COUNT) {
      error = sysfdobserve(&state->fds, (u32) item->fd, &view);
      if (error)
        break;
      if (view.tag.state == SYSFD_LIVE) {
        error = sysfilerefview(&state->files, state->files.self, view.file, &ref);
        if (!error)
          error = sysfilerefpin(&state->files, state->files.self, view.file);
        if (error)
          break;
        item->file = view.file;
        item->connection = ref.connection;
        item->row.session = ref.session;
        item->row.fd_generation = view.tag.generation;
        continue;
      }
    }
    item->row.revents = FS_POLL_NVAL;
  }
  if (error && sysfilepollrelease(state, items, i))
    return -ERR_STATE;
  return error;
}

isize
sysfilepollcurrent(const Fileimage *state, const Pollfile *item)
{
  Fdview view;
  isize  error;
  if (!item->file)
    return 0;
  error = sysfdobserve(&state->fds, (u32) item->fd, &view);
  if (error)
    return error;
  return view.tag.state == SYSFD_LIVE && view.tag.generation == item->row.fd_generation &&
         view.file == item->file;
}

isize
sysfilepollgroup(const Pollfile *items, u32 count, Conntag connection, Filepoll *rows, u32 *indices,
                 u32 capacity)
{
  u32 i, used = 0;
  if (!connection.generation || connection.index >= SYSCONN_COUNT || connection.reserved || !rows ||
      !indices || (count && !items))
    return -ERR_INVALID;
  for (i = 0; i < count; i++)
    if (items[i].file && items[i].connection.index == connection.index &&
        items[i].connection.generation == connection.generation) {
      if (used == MIN(capacity, FS_POLL_ROWS))
        return -ERR_CAPACITY;
      rows[used] = items[i].row;
      indices[used++] = i;
    }
  return used;
}

static isize
pollcleanup(Fileimage *state, Opkey keys[SYSCONN_COUNT])
{
  Conncache *cache = sysfileimagecache(state);
  isize      first = 0;
  u32        i;
  for (i = 0; i < SYSCONN_COUNT; i++) {
    Opkey    *key = &keys[i];
    Connwork *work = &state->connections[i];
    u64       pin = 0;
    isize     error;
    if (!key->lease.seq)
      continue;
    if (work->state.operations[key->lease.slot].phase == SYSOP_PREPARED) {
      error = sysopabort(work, cache, key, &pin);
      if (!error)
        error = sysfilerefunpin(&state->files, state->files.self, pin);
    } else
      error = sysfilepollabandon(state, key);
    if (error && !first)
      first = error;
  }
  fileprogress(state);
  return first;
}

static void
pollapply(Pollfile *items, const Filepoll *rows, const u32 *indices, u32 count, int ended)
{
  u32 i;
  for (i = 0; i < count; i++)
    items[indices[i]].row.revents = ended ? FS_POLL_ERR : rows[i].revents;
}

typedef struct Pollpending Pollpending;
struct Pollpending
{
  Fileimage *state;
  Opkey     *keys;
  u32        bits;
};

static void
pollinterrupt(void *context)
{
  const Pollpending *pending = context;
  Fileimage         *state = pending->state;
  Conncache         *cache = sysfileimagecache(state);
  u64                now;
  u32                i;
  if (systimernow(&now))
    return;
  for (i = 0; i < SYSCONN_COUNT; i++)
    if (pending->bits & (1U << i))
      (void) sysopinterrupt(&state->connections[i], cache, &pending->keys[i], now);
}

static isize
pollround(Fileimage *state, u32 layer, Pollfile *items, u32 count, u64 opcode, u64 deadline,
          u64 mask)
{
  Opkey      keys[SYSCONN_COUNT];
  Filepoll   rows[FS_POLL_ROWS];
  u32        indices[FS_POLL_ROWS], pending = 0, seen = 0, i;
  Conncache *cache = sysfileimagecache(state);
  u64        now;
  isize      error = 0, result = 0;
  for (i = 0; i < SYSCONN_COUNT; i++)
    keys[i].lease.seq = 0;
  if (opcode == FS_POLL_QUERY) {
    error = systimernow(&now);
    if (error || now > UINT64_MAX - SYSOP_CONTROL_NS)
      return error ? error : -ERR_OVERFLOW;
    deadline = now + SYSOP_CONTROL_NS;
  }
  for (i = 0; i < count && !error; i++) {
    Connentry entry;
    Conntag   tag = items[i].connection;
    Connwork *work;
    u64       pin;
    u32       bit;
    isize     n;
    if (!items[i].file)
      continue;
    error = sysconnview(cache, tag, &entry);
    if (error)
      break;
    bit = 1U << tag.index;
    if (seen & bit)
      continue;
    seen |= bit;
    n = sysfilepollgroup(items, count, tag, rows, indices, ARRAY_SIZE(rows));
    if (n <= 0) {
      error = n ? n : -ERR_STATE;
      break;
    }
    if (entry.state == SYSCONN_DRAINING) {
      pollapply(items, rows, indices, (u32) n, 1);
      continue;
    }
    if (entry.state != SYSCONN_READY) {
      error = -ERR_BUSY;
      break;
    }
    work = &state->connections[tag.index];
    pin = items[i].file;
    error = sysfilerefpin(&state->files, state->files.self, pin);
    if (error)
      break;
    error = sysoppollprepare(work, cache, layer, 1, &pin, opcode, deadline, rows, (u32) n,
                             &keys[tag.index]);
    if (pin && sysfilerefunpin(&state->files, state->files.self, pin))
      error = -ERR_STATE;
    if (!error)
      error = sysfilepollsubmit(work, cache, &keys[tag.index]);
    if (error == -ERR_PEER && keys[tag.index].lease.seq) {
      error = sysconninvalidate(cache, tag);
      if (!error) {
        work->state.ended = 1;
        pollapply(items, rows, indices, (u32) n, 1);
      }
    } else if (!error)
      pending |= bit;
  }
  while (!error && pending) {
    Pollpending interrupt;
    for (i = 0; i < SYSCONN_COUNT; i++) {
      Fileioresult reply;
      Connwork    *work = &state->connections[i];
      Opkey       *key = &keys[i];
      u64          pin = 0;
      isize        n;
      if (!(pending & (1U << i)))
        continue;
      n = sysfilepollgroup(items, count, key->connection, rows, indices, ARRAY_SIZE(rows));
      if (n <= 0) {
        error = n ? n : -ERR_STATE;
        break;
      }
      error = sysfilepollcollect(work, cache, key, rows, (u32) n, &reply, &pin);
      if (error == -ERR_WOULD_BLOCK && work->state.ended) {
        /* Invalidation is independent of FINISH. A collectable result above
           wins; an unfinished request still owns its slot until drain. */
        error = sysfilepollabandon(state, key);
        reply.native = -ERR_PEER;
        reply.business = 0;
      }
      if (error == -ERR_WOULD_BLOCK) {
        error = 0;
        continue;
      }
      if (pin && sysfilerefunpin(&state->files, state->files.self, pin))
        error = -ERR_STATE;
      if (error)
        break;
      pending &= ~(1U << i);
      if (reply.native && reply.native != -ERR_PEER)
        error = reply.native;
      else if (reply.business)
        error = reply.business == -EAGAIN ? -ERR_BUSY : -ERR_STATE;
      else
        pollapply(items, rows, indices, (u32) n, reply.native == -ERR_PEER);
      if (error || opcode == FS_POLL_WAIT) {
        result = 1;
        break;
      }
    }
    if (error || result || !pending)
      break;
    if (deadline) {
      error = systimernow(&now);
      if (error || now >= deadline) {
        if (!error && opcode == FS_POLL_QUERY)
          error = -ERR_TIMEDOUT;
        break;
      }
    }
    interrupt.state = state;
    interrupt.keys = keys;
    interrupt.bits = pending;
    error = systimerwaitcancel(deadline, EVENT_RQ_DONE, mask, pollinterrupt, &interrupt);
    fileprogress(state);
  }
  if (pollcleanup(state, keys) && !error)
    error = -ERR_STATE;
  return error ? error : result;
}

isize
sysfileimagepoll(u32 layer, Pollfd *fds, u32 count, int timeout)
{
  Fileimage *state = NULL;
  Pollfile   items[FS_POLL_ROWS];
  u64        mask, ignored, limit = 0, now;
  u32        i, restart, live = 0;
  isize      error, restored, ready = 0;
  if (layer >= RCTX_LAYERS || count > FS_POLL_ROWS || (count && !fds))
    return -ERR_INVALID;
  if (timeout > 0) {
    u64 duration = (u64) timeout * UINT64_C(1000000);
    error = systimernow(&now);
    if (error || now > UINT64_MAX - duration)
      return error ? error : -ERR_OVERFLOW;
    limit = now + duration;
  }
  error = __sys_sigmask(SIGNAL_BLOCK, SIGNAL_SUPPORTED, &mask);
  if (error)
    return error;
  if (count) {
    error = sysfileimage(&state);
    if (!error) {
      fileprogress(state);
      error = sysfilepollcapture(state, fds, items, count);
    }
    if (error)
      goto restore;
    for (i = 0; i < count; i++)
      live |= !!items[i].file;
  }
  for (;;) {
    if (live)
      error = pollround(state, layer, items, count, FS_POLL_QUERY, 0, mask);
    if (error)
      break;
    ready = 0;
    for (i = 0; i < count; i++) {
      if (items[i].file) {
        error = sysfilepollcurrent(state, &items[i]);
        if (error < 0)
          break;
        if (!error)
          items[i].row.revents = FS_POLL_NVAL;
        error = 0;
      }
      ready += !!items[i].row.revents;
    }
    if (error || ready || !timeout)
      break;
    if (limit) {
      error = systimernow(&now);
      if (error || now >= limit)
        break;
    }
    if (live) {
      error = pollround(state, layer, items, count, FS_POLL_WAIT, limit, mask);
      if (error < 0)
        break;
      error = 0;
    } else {
      error = systimerwaitsignal(limit, EVENT_RQ_DONE, mask, &restart);
      if (error)
        break;
    }
  }
  if (count && sysfilepollrelease(state, items, count) && !error)
    error = -ERR_STATE;
  if (!error)
    for (i = 0; i < count; i++)
      fds[i].revents = (u16) items[i].row.revents;
restore:
  restored = __sys_sigmask(SIGNAL_SETMASK, mask, &ignored);
  return error ? error : restored ? restored : ready;
}

Conncache *
sysfileimagecache(Fileimage *state)
{
  return (Conncache *) (void *) state->connections[0].reserved;
}

isize
sysfileimage(Fileimage **out)
{
  Connpeer self;
  Handle   parent;
  u64      mask, ignored;
  isize    error, restored;
  if (!out || !sysboot()->header)
    return -ERR_INVALID;
  error = __sys_sigmask(SIGNAL_BLOCK, SIGNAL_SUPPORTED, &mask);
  if (error)
    return error;
  error = __sys_getid(&self.pid, &self.image, &parent);
  if (!error && initialized)
    error = sysconncheck(sysfileimagecache(&image), self);
  else if (!error) {
    error = sysconninit(sysfileimagecache(&image), self);
    if (!error)
      error = sysfilerefinit(&image.files, self);
    if (!error)
      error = sysctxinit(&image.contexts, self);
    if (!error)
      error = sysctxsigbind(&image.contexts);
    if (!error) {
      sysfdinit(&image.fds);
      initialized = 1;
      systimerbindrq(fileevents);
    }
  }
  restored = __sys_sigmask(SIGNAL_SETMASK, mask, &ignored);
  if (!error && !restored)
    *out = &image;
  return error ? error : restored;
}

static isize
preparefs(Conncache *cache, Handle call)
{
  Connpeer  peer;
  Conntag   tag;
  Connclaim claim;
  Handle    redundant = 0;
  Connwork *work;
  isize     error;
  if (!fstag.generation) {
    error = sysconnpeer(call, &peer);
    if (!error)
      error = sysconnbind(cache, peer, &tag);
    if (error)
      return error;
    work = &image.connections[tag.index];
    error = sysconnworkinit(work, cache, tag);
    if (!error)
      error = sysconnworktakecall(work, cache, &call, &redundant);
    if (error)
      return error;
    fstag = tag;
  }
  work = &image.connections[fstag.index];
  if (image.contexts.sequence == UINT64_MAX)
    return -ERR_OVERFLOW;
  error = sysconnbegin(cache, fstag, image.contexts.sequence + 1, &claim);
  if (!error) {
    error = sysbootprepare(&work->state.bootstrap, claim, FS_HELLO, UINT64_MAX, 0);
    if (error)
      (void) sysconnrollback(cache, claim);
  }
  return error;
}

isize
sysfileimagefs(u32 layer, u64 address, Connwork **out)
{
  const Bootview *boot = sysboot();
  Fileimage      *state;
  Conncache      *cache;
  Connentry       entry;
  Connwork       *work = NULL;
  Handle          call, space;
  u64             mask, ignored;
  isize           error, restored;
  if (!out || layer >= RCTX_LAYERS || !boot->header)
    return -ERR_INVALID;
  call = bootresource(boot, BOOT_FS_CALL);
  space = bootresource(boot, BOOT_SELF_SPACE);
  if (!call)
    return -ERR_NOTSUP;
  if (!space)
    return -ERR_STATE;
  error = sysfileimage(&state);
  if (error)
    return error;
  cache = sysfileimagecache(state);
  error = __sys_sigmask(SIGNAL_BLOCK, SIGNAL_SUPPORTED, &mask);
  if (error)
    return error;
  fileprogress(state);
  if (connecting) {
    error = -ERR_BUSY;
    goto restore;
  }
  if (fstag.generation) {
    error = sysconnview(cache, fstag, &entry);
    if (error)
      goto restore;
    if (entry.state == SYSCONN_READY) {
      work = &state->connections[fstag.index];
      goto restore;
    }
    if (entry.state == SYSCONN_DRAINING) {
      error = -ERR_PEER;
      goto restore;
    }
  }
  if (!fstag.generation || entry.state == SYSCONN_UNCLAIMED) {
    error = preparefs(cache, call);
    if (error)
      goto restore;
  }
  work = &state->connections[fstag.index];
  connecting = 1;
  error = __sys_sigmask(SIGNAL_SETMASK, mask, &ignored);
  if (!error)
    error = sysctxhello(&state->contexts, layer, work, cache, space, address, &candidate, &hello);
  restored = __sys_sigmask(SIGNAL_BLOCK, SIGNAL_SUPPORTED, &mask);
  connecting = 0;
  if (restored)
    return error ? error : restored;
restore:
  restored = __sys_sigmask(SIGNAL_SETMASK, mask, &ignored);
  if (!error && !restored)
    *out = work;
  return error ? error : restored;
}

/* Inspect the directory while caught signals are blocked. */
static isize
readyfs(Fileimage *state, Connwork **out)
{
  Connentry entry;
  isize     error;
  if (!fstag.generation)
    return -ERR_NOTSUP;
  error = sysconnview(sysfileimagecache(state), fstag, &entry);
  if (error)
    return error;
  if (entry.state != SYSCONN_READY)
    return entry.state == SYSCONN_DRAINING ? -ERR_PEER : -ERR_NOTSUP;
  *out = &state->connections[fstag.index];
  return 0;
}

isize
sysfileimagepipe(u32 layer, Connwork *work, Pipecreate *pipe, Rkey *active, int *fds,
                 isize *business)
{
  Fileimage *state;
  u64        mask, ignored;
  isize      error, restored;
  error = sysfileimage(&state);
  if (error)
    return error;
  if (!fstag.generation || work != &state->connections[fstag.index])
    return -ERR_INVALID;
  error = sysctxpipecreate(&state->contexts, layer, &state->files, work, sysfileimagecache(state),
                           &state->fds, pipe, active, fds, business);
  if (error != -ERR_FAULT)
    return error;
  /* Failed descriptor copyout leaves both remote references on the close queue. */
  if (!__sys_sigmask(SIGNAL_BLOCK, SIGNAL_SUPPORTED, &mask)) {
    closing = 1;
    fileprogress(state);
    restored = __sys_sigmask(SIGNAL_SETMASK, mask, &ignored);
    if (restored)
      return restored;
  }
  return error;
}

isize
sysfileimagepipepair(u32 layer, int *fds, isize *business)
{
  Fileimage *state;
  Conncache *cache;
  Connwork  *work;
  Pipecreate pipe;
  Rkey       active = {0};
  u64        mask, ignored;
  u32        i;
  isize      error, restored;
  if (!fds)
    return -ERR_FAULT;
  error = sysfileimage(&state);
  if (error)
    return error;
  error = __sys_sigmask(SIGNAL_BLOCK, SIGNAL_SUPPORTED, &mask);
  if (error)
    return error;
  fileprogress(state);
  error = readyfs(state, &work);
  restored = __sys_sigmask(SIGNAL_SETMASK, mask, &ignored);
  if (error || restored)
    return error ? error : restored;

  cache = sysfileimagecache(state);
  for (i = 0; i < sizeof(pipe); i++)
    ((volatile u8 *) &pipe)[i] = 0;
  do {
    /* The handler may run, but the continuation still owns the original pair. */
    error = sysfileimagepipe(layer, work, &pipe, &active, fds, business);
  } while (error == -ERR_INTERRUPTED);
  if (!error || !pipe.operation.lease.seq || active.sequence)
    return error;

  /* A late result cannot publish descriptors after the caller returns. */
  if (!__sys_sigmask(SIGNAL_BLOCK, SIGNAL_SUPPORTED, &mask)) {
    if (!sysfilepipeabandon(&state->files, work, cache, &state->fds, &pipe)) {
      draining |= 1U << work->state.tag.index;
      fileprogress(state);
    }
    restored = __sys_sigmask(SIGNAL_SETMASK, mask, &ignored);
    if (restored)
      return restored;
  }
  return error;
}

isize
sysfileimageopen(u32 layer, Connwork *work, const char *path, u64 flags, u64 mode, u64 fdflags,
                 Opencreate *open, Rkey *active, int *fd, isize *business)
{
  Fileimage *state;
  Conncache *cache;
  u64        mask, ignored;
  isize      error, restored;
  error = sysfileimage(&state);
  if (error)
    return error;
  if (!fstag.generation || work != &state->connections[fstag.index])
    return -ERR_INVALID;
  cache = sysfileimagecache(state);
  error = sysctxopencreate(&state->contexts, layer, &state->files, work, cache, &state->fds, path,
                           flags, mode, fdflags, open, active, fd, business);
  if (error != -ERR_TIMEDOUT)
    return error;
  error = __sys_sigmask(SIGNAL_BLOCK, SIGNAL_SUPPORTED, &mask);
  if (error)
    return error;
  error = sysfileopenabandon(&state->files, work, cache, &state->fds, open);
  if (!error) {
    draining |= 1U << work->state.tag.index;
    fileprogress(state);
  }
  restored = __sys_sigmask(SIGNAL_SETMASK, mask, &ignored);
  return restored ? restored : error ? error : -ERR_TIMEDOUT;
}

isize
sysfileimageopenpath(u32 layer, const char *path, u64 flags, u64 mode, isize *business)
{
  Fileimage *state;
  Connwork  *work;
  Opencreate open;
  Rkey       active = {0};
  u64        mask, ignored;
  u32        i;
  int        fd = -1;
  isize      error, restored;
  error = sysfileimage(&state);
  if (error)
    return error;
  error = __sys_sigmask(SIGNAL_BLOCK, SIGNAL_SUPPORTED, &mask);
  if (error)
    return error;
  fileprogress(state);
  error = readyfs(state, &work);
  restored = __sys_sigmask(SIGNAL_SETMASK, mask, &ignored);
  if (error || restored)
    return error ? error : restored;
  for (i = 0; i < sizeof(open); i++)
    ((volatile u8 *) &open)[i] = 0;
  error = sysfileimageopen(layer, work, path, flags, mode,
                           flags & FS_OPEN_CLOEXEC ? BOOT_FD_CLOEXEC : 0, &open, &active, &fd,
                           business);
  if (!error)
    return *business ? 0 : fd;
  if (open.operation.lease.seq && !active.sequence) {
    Conncache *cache = sysfileimagecache(state);
    Slotop    *op = &work->state.operations[open.operation.lease.slot];
    u64        now;
    isize      cleanup = __sys_sigmask(SIGNAL_BLOCK, SIGNAL_SUPPORTED, &mask);
    if (cleanup)
      return cleanup;
    if (op->phase == SYSOP_WAITING && !(op->flags & SYSOP_CANCEL_SENT)) {
      cleanup = systimernow(&now);
      if (!cleanup)
        cleanup = sysfileopeninterrupt(&state->files, work, cache, &state->fds, &open, now);
    }
    if (!cleanup)
      cleanup = sysfileopenabandon(&state->files, work, cache, &state->fds, &open);
    if (!cleanup) {
      draining |= 1U << work->state.tag.index;
      fileprogress(state);
    }
    restored = __sys_sigmask(SIGNAL_SETMASK, mask, &ignored);
    if (cleanup || restored)
      return cleanup ? cleanup : restored;
  }
  /* The installed connection has no allocator for a delivered backend mapping. */
  return error == 1 ? -ERR_NOTSUP : error;
}

static int
ioopcode(u64 opcode)
{
  return opcode == FS_READ || opcode == FS_WRITE || opcode == FS_SEEK || opcode == FS_TTYCTL ||
         opcode == FS_SYNC || opcode == FS_PATH;
}

static isize
iostage(Fileimage *state, u32 layer, Connwork *work, u64 pin, u64 session, u64 opcode,
        const void *input, u64 bytes, u64 offset, u32 positional, u32 pathcapacity, u32 replay,
        Opkey *key)
{
  Fileslot   descriptor;
  Conncache *cache = sysfileimagecache(state);
  Opkey      draft;
  u64        base;
  u8        *snapshot = state->contexts.snapshots[layer];
  u32        i;
  isize      error;
  error = sysopprepare(work, cache, layer, 1, state->contexts.sequence + 1, &pin, session, opcode,
                       0, &draft);
  if (!error) {
    base = work->state.map.address;
    if ((opcode == FS_WRITE || (opcode == FS_PATH && offset != FS_PATH_GETCWD)) && bytes) {
      if (opcode == FS_WRITE && !replay)
        error = __sys_copy(snapshot, input, bytes);
      if (!error)
        error = __sys_copy((void *) (base + FS_DATA_OFFSET + draft.lease.slot * FS_SLOT_BYTES),
                           snapshot, bytes);
    }
    if (!error) {
      for (i = 0; i < sizeof(descriptor); i++)
        ((volatile u8 *) &descriptor)[i] = 0;
      descriptor.seq = draft.lease.seq;
      descriptor.state = FS_SLOT_SUBMITTED;
      descriptor.payload_bytes = bytes;
      if (opcode == FS_TTYCTL) {
        descriptor.args[0] = TTY_GETATTR;
        descriptor.args[4] = sizeof(Termarg);
      } else if (opcode == FS_PATH) {
        if (offset == FS_PATH_GETCWD) {
          descriptor.args[0] = FS_PATH_GETCWD;
          descriptor.args[1] = bytes;
          descriptor.payload_bytes = 0;
        } else if (offset == FS_PATH_READLINK) {
          descriptor.args[0] = FS_PATH_READLINK;
          descriptor.args[2] = bytes;
          descriptor.args[3] = pathcapacity;
        } else {
          descriptor.args[0] = FS_PATH_CHDIR;
          descriptor.args[2] = bytes;
        }
      } else {
        descriptor.args[0] = offset;
        descriptor.args[1] = opcode == FS_SEEK ? positional : bytes;
        descriptor.args[3] = opcode != FS_SEEK && positional ? FS_IO_POSITIONAL : 0;
      }
      descriptor.object = session;
      descriptor.span = 1;
      error = __sys_copy((void *) (base + FS_DESC_OFFSET + draft.lease.slot * FS_DESC_BYTES),
                         &descriptor, sizeof(descriptor));
    }
    if (!error) {
      for (i = 0; i < sizeof(draft); i++)
        ((volatile u8 *) key)[i] = ((const u8 *) &draft)[i];
      return 0;
    }
    if (sysopabort(work, cache, &draft, &pin))
      return -ERR_STATE;
  }
  return pin && sysfilerefunpin(&state->files, state->files.self, pin) ? -ERR_STATE : error;
}

static isize
stagepath(Fileimage *state, u32 layer, const char *path, u64 subcode, Connwork **work, Opkey *key,
          u64 *bytes, u32 capacity, isize *business)
{
  isize error;
  if (layer >= RCTX_LAYERS)
    return -ERR_INVALID;
  if (state->contexts.contexts[layer].phase != RCTX_FREE)
    return -ERR_BUSY;
  if (state->contexts.sequence == UINT64_MAX)
    return -ERR_OVERFLOW;
  if (subcode != FS_PATH_GETCWD) {
    error = sysargstring(state->contexts.snapshots[layer], FS_SLOT_BYTES, path, bytes);
    if (error == -ERR_LIMIT || (!error && *bytes == 1)) {
      *business = error ? -ENAMETOOLONG : -ENOENT;
      return 0;
    }
    if (error)
      return error;
  }
  error = readyfs(state, work);
  return error ? error
               : iostage(state, layer, *work, 0, 0, FS_PATH, NULL, *bytes, subcode, 0, capacity, 0,
                         key);
}

static isize
stagefd(Fileimage *state, u32 layer, int fd, u64 opcode, const void *input, u64 bytes, u64 offset,
        u32 positional, Connwork **out, Opkey *key)
{
  Fdview    view;
  Fileref   ref;
  Connwork *work;
  isize     error;
  if (!state || !out || !key || layer >= RCTX_LAYERS || bytes > FS_SLOT_BYTES ||
      !ioopcode(opcode) || (opcode == FS_TTYCTL && bytes != sizeof(Termarg)) ||
      (opcode == FS_SYNC && (bytes || offset > 1 || positional)))
    return -ERR_INVALID;
  if (fd < 0 || (u32) fd >= BOOT_FD_COUNT)
    return -ERR_BAD_HANDLE;
  error = sysfdobserve(&state->fds, (u32) fd, &view);
  if (error || view.tag.state != SYSFD_LIVE)
    return error ? error : -ERR_BAD_HANDLE;
  if (state->contexts.contexts[layer].phase != RCTX_FREE)
    return -ERR_BUSY;
  if (state->contexts.sequence == UINT64_MAX)
    return -ERR_OVERFLOW;
  error = sysfilerefview(&state->files, state->files.self, view.file, &ref);
  if (error)
    return error;
  if (ref.connection.index >= SYSCONN_COUNT)
    return -ERR_STATE;
  work = &state->connections[ref.connection.index];
  if (work->state.tag.generation != ref.connection.generation)
    return -ERR_STALE;
  error = sysfilerefpin(&state->files, state->files.self, view.file);
  if (!error) {
    error = iostage(state, layer, work, view.file, ref.session, opcode, input, bytes, offset,
                    positional, 0, 0, key);
    if (!error)
      *out = work;
  }
  return error;
}

isize
sysfileiostage(Fileimage *state, u32 layer, int fd, u64 opcode, const void *input, u64 bytes,
               Connwork **out, Opkey *key)
{
  return stagefd(state, layer, fd, opcode, input, bytes, 0, 0, out, key);
}

isize
sysfileiowait(Fileimage *state, u32 layer, const Opkey *key, u64 mask, u32 *restart)
{
  Conncache *cache;
  Connwork  *work;
  Connpeer   self;
  Slotop    *op;
  Rkey       context;
  u64        now;
  isize      error, status;
  if (!state || !key || !key->owner || key->connection.index >= SYSCONN_COUNT ||
      layer >= RCTX_LAYERS)
    return -ERR_INVALID;
  work = &state->connections[key->connection.index];
  cache = sysfileimagecache(state);
  self = work->state.self;
  error = sysopcheck(work, cache, key);
  if (error)
    return error;
  op = &work->state.operations[key->lease.slot];
  if (!ioopcode(op->opcode))
    return -ERR_INVALID;
  if (op->phase == SYSOP_PREPARED) {
    error = sysctxopcall(&state->contexts, self, layer, work, cache, key, 0, &context);
    if (error)
      return error;
    status = sysctxinvoke(&state->contexts, self, context);
    error = sysctxhandoff(&state->contexts, self, context);
    if (!error && status == -ERR_PEER)
      error = sysctxpeerended(&state->contexts, self, context);
    if (!error)
      error = sysctxfinish(&state->contexts, self, context);
    if (error || status)
      return error ? error : status;
  }
  for (;;) {
    u64 deadline;
    fileprogress(state);
    error = sysoppoll(work, cache, key);
    if (!error)
      error = sysopfinished(work, cache, key);
    if (error != -ERR_WOULD_BLOCK)
      return error;
    if (op->phase == SYSOP_RESULT && op->status == -ERR_PEER)
      return -ERR_PEER;
    deadline = op->phase == SYSOP_WAITING ? op->deadline : 0;
    if (deadline) {
      error = systimernow(&now);
      if (error || now >= deadline)
        return error ? error : -ERR_TIMEDOUT;
    }
    if (op->phase == SYSOP_WAITING) {
      /* Bind the exact RPC for signal cancellation, but wait on its event.
         The event wait guard also covers a signal arriving before recv. */
      error = sysctxopwait(&state->contexts, self, layer, work, cache, key, &context);
      if (error)
        return error;
      status = sysctxinterruptible(&state->contexts, self, context);
      if (!status)
        status = systimerwaitsignal(deadline, EVENT_RQ_DONE, mask, restart);
      error = sysctxabort(&state->contexts, self, context);
      if (error)
        return error;
    } else
      status = systimerwaitsignal(deadline, EVENT_RQ_DONE, mask, restart);
    if (status)
      return status;
  }
}

isize
sysfileioresume(Fileimage *state, Opkey *key)
{
  Opkey     background;
  Connwork *work;
  u64       now;
  isize     error;
  if (state != &image || !key || key->connection.index >= SYSCONN_COUNT)
    return -ERR_INVALID;
  work = &state->connections[key->connection.index];
  error = sysopcheck(work, sysfileimagecache(state), key);
  if (error)
    return error;
  if (!ioopcode(work->state.operations[key->lease.slot].opcode))
    return -ERR_INVALID;
  error = systimernow(&now);
  if (!error)
    error = sysopresume(work, sysfileimagecache(state), key, now, &background);
  if (error == SYSOP_ABANDONED)
    draining |= 1U << background.connection.index;
  return error;
}

static isize
restartio(Fileimage *state, Opkey *key, u64 bytes, u64 offset, u32 positional, u32 pathcapacity)
{
  Conncache *cache;
  Connwork  *work;
  Slotop    *op;
  u64        file = 0, session, opcode;
  u32        layer;
  isize      error;
  if (!state || !key || !key->owner || key->connection.index >= SYSCONN_COUNT ||
      key->lease.span != 1 || bytes > FS_SLOT_BYTES)
    return -ERR_INVALID;
  layer = key->lease.slot / 2;
  if (layer >= RCTX_LAYERS || state->contexts.contexts[layer].phase != RCTX_FREE)
    return -ERR_STATE;
  if (state->contexts.sequence == UINT64_MAX)
    return -ERR_OVERFLOW;
  cache = sysfileimagecache(state);
  work = &state->connections[key->connection.index];
  error = sysopcheck(work, cache, key);
  if (error)
    return error;
  op = &work->state.operations[key->lease.slot];
  if (!ioopcode(op->opcode))
    return -ERR_INVALID;
  error = sysoppoll(work, cache, key);
  if (error)
    return error;
  /* SYNC and CHDIR replay only when native cancellation proves no delivery. */
  if (op->kind != IPC_NONE || op->resource || op->flags & SYSOP_BAD_RESULT ||
      !(op->flags & SYSOP_CANCEL_SENT) || op->words[2] || op->words[3] ||
      ((op->opcode == FS_SYNC || op->opcode == FS_PATH) && op->status != -ERR_CANCELLED) ||
      (op->status == -ERR_CANCELLED
           ? (op->words[0] || op->words[1])
           : (op->status || (isize) op->words[0] != -EINTR || op->words[1] != key->lease.seq)))
    return -ERR_STATE;
  error = sysopfinished(work, cache, key);
  if (error)
    return error;
  session = op->object;
  opcode = op->opcode;
  if (op->phase != SYSOP_ACKED) {
    error = sysopsaved(work, cache, key);
    if (!error)
      error = sysopack(work, cache, key);
    if (error)
      return error;
  }
  error = sysopfinish(work, cache, key, &file);
  if (error)
    return error;
  key->lease.seq = 0;
  return iostage(state, layer, work, file, session, opcode, NULL, bytes, offset, positional,
                 pathcapacity, 1, key);
}

isize
sysfileiorestart(Fileimage *state, Opkey *key, u64 bytes)
{
  return restartio(state, key, bytes, 0, 0, 0);
}

static isize
copycwd(Fileimage *state, Connwork *work, Conncache *cache, const Opkey *key, u64 bytes,
        void *output)
{
  u8   *path = state->contexts.snapshots[key->lease.slot / 2];
  u64   i;
  u64   start = (uintptr_t) output, mapping = work->state.map.address;
  isize error;
  if (start < mapping + FS_CONN_BYTES && mapping < start + bytes)
    return -ERR_FAULT;
  error = sysopcopy(work, cache, key, 0, bytes, path);
  if (error)
    return error;
  if (path[0] != '/' || path[bytes - 1])
    return -ERR_STATE;
  for (i = 1; i + 1 < bytes; i++)
    if (!path[i])
      return -ERR_STATE;
  return __sys_copy(output, path, bytes);
}

static isize
fileiofinish(Fileimage *state, const Opkey *key, void *output, u64 bytes, u64 subcode,
             Fileioresult *result)
{
  Conncache *cache;
  Connwork  *work;
  Slotop    *op;
  u64        file = 0, address, storage, expected, value_limit;
  isize      error;
  u32        cwd, readlink;
  if (!state || !key || !result || !key->owner || key->connection.index >= SYSCONN_COUNT ||
      key->lease.span != 1 || bytes > FS_SLOT_BYTES)
    return -ERR_INVALID;
  cache = sysfileimagecache(state);
  work = &state->connections[key->connection.index];
  error = sysopcheck(work, cache, key);
  if (error)
    return error;
  op = &work->state.operations[key->lease.slot];
  if (!ioopcode(op->opcode) || (op->opcode == FS_TTYCTL && bytes != sizeof(Termarg)) ||
      (op->opcode == FS_SYNC && bytes))
    return -ERR_INVALID;
  cwd = op->opcode == FS_PATH && subcode == FS_PATH_GETCWD;
  readlink = op->opcode == FS_PATH && subcode == FS_PATH_READLINK;
  if ((cwd || readlink) && key->lease.slot / 2 >= RCTX_LAYERS)
    return -ERR_INVALID;
  if (readlink && !bytes)
    return -ERR_INVALID;
  error = sysoppoll(work, cache, key);
  if (!error)
    error = sysopfinished(work, cache, key);
  if (error)
    return error;
  if (!(op->flags & SYSOP_SAVED)) {
    expected = 0;
    value_limit = bytes;
    if (op->opcode == FS_READ)
      expected = op->words[2];
    else if (op->opcode == FS_SEEK)
      value_limit = INT64_MAX;
    else if (op->opcode == FS_TTYCTL) {
      value_limit = 0;
      if (!op->words[0])
        expected = sizeof(Termarg);
    } else if (cwd || readlink) {
      value_limit = 0;
      expected = op->words[3];
    }
    result->native = result->business = 0;
    result->count = 0;
    if (op->kind != IPC_NONE || op->resource || op->flags & SYSOP_BAD_RESULT)
      result->native = -ERR_STATE;
    else if (op->status) {
      if (op->words[0] || op->words[1] || op->words[2] || op->words[3])
        result->native = -ERR_STATE;
      else {
        if (op->status == -ERR_PEER && !work->state.ended) {
          error = sysoppeerended(work, cache, key);
          if (error)
            return error;
        }
        result->native = op->status;
      }
    } else if (op->words[1] != key->lease.seq || (isize) op->words[0] > 0 ||
               (op->words[0] && (op->words[2] || op->words[3])) || op->words[2] > value_limit ||
               op->words[3] != expected ||
               (readlink && !op->words[0] && (!expected || expected > bytes)) ||
               (cwd && !op->words[0] && (expected < 2 || expected > bytes)))
      result->native = -ERR_STATE;
    else {
      result->business = (isize) op->words[0];
      result->count = readlink ? expected : op->words[2];
      if (op->words[3]) {
        address = (uintptr_t) output;
        storage = (uintptr_t) state;
        if (address > UINT64_MAX - expected ||
            (address < storage + sizeof(*state) && storage < address + expected))
          error = -ERR_FAULT;
        else {
          storage = (uintptr_t) result;
          if (address < storage + sizeof(*result) && storage < address + expected)
            error = -ERR_FAULT;
          else if (cwd)
            error = copycwd(state, work, cache, key, expected, output);
          else
            error = sysopcopy(work, cache, key, 0, expected, output);
        }
        if (error) {
          result->native = error == -ERR_INVALID ? -ERR_FAULT : error;
          result->count = 0;
        }
      }
    }
    if (op->resource) {
      Handle dropped;
      error = __sys_free(op->resource);
      if (error)
        return error;
      error = sysopresource(work, cache, key, &dropped);
      if (error)
        return error;
    }
    error = sysopsaved(work, cache, key);
    if (error)
      return error;
  }
  if (op->phase != SYSOP_ACKED) {
    error = sysopack(work, cache, key);
    if (error)
      return error;
  }
  error = sysopfinish(work, cache, key, &file);
  if (!error && file)
    error = sysfilerefunpin(&state->files, work->state.self, file);
  if (!error)
    fileprogress(state);
  return error;
}

isize
sysfileiofinish(Fileimage *state, const Opkey *key, void *output, u64 bytes, Fileioresult *result)
{
  return fileiofinish(state, key, output, bytes, 0, result);
}

static isize
imageio(u32 layer, int fd, u64 opcode, const void *input, void *output, u64 bytes, u64 offset,
        u32 positional, Fileioresult *result)
{
  Fileimage *state;
  Connwork  *work;
  Conncache *cache;
  Slotop    *op;
  Opkey      key, background;
  u64        mask, ignored, file;
  u64        payload;
  u32        restart, interrupted = 0, retry = 1;
  isize      error, discard, business = 0;
  error = sysfileimage(&state);
  if (error)
    return error;
  error = __sys_sigmask(SIGNAL_BLOCK, SIGNAL_SUPPORTED, &mask);
  if (error)
    return error;
  fileprogress(state);
  bytes = MIN(bytes, FS_SLOT_BYTES);
  payload = bytes;
  if (opcode == FS_PATH) {
    error = stagepath(state, layer, input, offset, &work, &key, &payload, bytes, &business);
    if (business) {
      result->native = result->count = 0;
      result->business = business;
      goto restore;
    }
  } else
    error = stagefd(state, layer, fd, opcode, input, bytes, offset, positional, &work, &key);
  if (error)
    goto restore;
  cache = sysfileimagecache(state);
  for (;;) {
    restart = 0;
    error = sysfileiowait(state, layer, &key, mask, &restart);
    if (error == -ERR_INTERRUPTED) {
      interrupted = 1;
      retry &= restart;
      continue;
    }
    if (!error && interrupted && retry) {
      error = restartio(state, &key, opcode == FS_PATH ? payload : bytes, offset, positional,
                        opcode == FS_PATH ? bytes : 0);
      if (!error) {
        interrupted = 0;
        continue;
      }
      if (error == -ERR_STATE && key.lease.seq)
        error = 0; /* Fixed result takes precedence over restart. */
    }
    if (!error) {
      error = fileiofinish(state, &key, output, bytes, offset, result);
      if (!error)
        break;
    }
    if (!key.lease.seq || sysopcheck(work, cache, &key))
      break;
    op = &work->state.operations[key.lease.slot];
    if (op->phase == SYSOP_PREPARED) {
      discard = sysopabort(work, cache, &key, &file);
      if (!discard && file)
        discard = sysfilerefunpin(&state->files, work->state.self, file);
      if (discard)
        error = discard;
      break;
    }
    if (error == -ERR_TIMEDOUT) {
      discard = sysfileioresume(state, &key);
      if (!discard)
        continue;
      if (discard == SYSOP_ABANDONED)
        break;
    }
    if (op->phase == SYSOP_RESULT && op->status == -ERR_PEER && !work->state.ended)
      (void) sysoppeerended(work, cache, &key);
    if (!sysopabandon(work, cache, &key, &background))
      draining |= 1U << background.connection.index;
    break;
  }
restore:
  discard = __sys_sigmask(SIGNAL_SETMASK, mask, &ignored);
  return error ? error : discard;
}

isize
sysfileimageio(u32 layer, int fd, u64 opcode, const void *input, void *output, u64 bytes,
               Fileioresult *result)
{
  return imageio(layer, fd, opcode, input, output, bytes, 0, 0, result);
}

isize
sysfileimageposio(u32 layer, int fd, u64 opcode, const void *input, void *output, u64 bytes,
                  int64_t offset, Fileioresult *result)
{
  if (offset < 0)
    return -ERR_INVALID;
  return imageio(layer, fd, opcode, input, output, bytes, (u64) offset, 1, result);
}

isize
sysfileimageseek(u32 layer, int fd, int64_t offset, int whence, Fileioresult *result)
{
  if ((u32) whence > FS_SEEK_END)
    return -ERR_INVALID;
  return imageio(layer, fd, FS_SEEK, NULL, NULL, 0, (u64) offset, (u32) whence, result);
}

isize
sysfileimagesync(u32 layer, int fd, u32 dataonly, Fileioresult *result)
{
  return imageio(layer, fd, FS_SYNC, NULL, NULL, 0, dataonly, 0, result);
}

isize
sysfileimagechdir(u32 layer, const char *path, Fileioresult *result)
{
  return imageio(layer, -1, FS_PATH, path, NULL, 0, 0, 0, result);
}

isize
sysfileimagereadlink(u32 layer, const char *path, char *buffer, u64 bytes, Fileioresult *result)
{
  if (!bytes)
    return -ERR_INVALID;
  return imageio(layer, -1, FS_PATH, path, buffer, bytes, FS_PATH_READLINK, 0, result);
}

isize
sysfileimagegetcwd(u32 layer, void *output, u64 bytes, Fileioresult *result)
{
  if (!bytes)
    return -ERR_INVALID;
  return imageio(layer, -1, FS_PATH, NULL, output, bytes, FS_PATH_GETCWD, 0, result);
}

isize
sysfileimagegetattr(u32 layer, int fd, void *output, Fileioresult *result)
{
  return imageio(layer, fd, FS_TTYCTL, NULL, output, sizeof(Termarg), 0, 0, result);
}

isize
sysfileclosewait(Filepool *pool, Connwork *work, Conncache *cache, const Fileclose *anchor,
                 Opkey *key, Opkey *background, u64 mask)
{
  u64   now, deadline;
  isize error = sysopcheck(work, cache, key);
  if (error)
    return error;
  deadline = work->state.operations[key->lease.slot].deadline;
  if (!deadline)
    return -ERR_STATE;
  for (;;) {
    error = systimernow(&now);
    /* A fixed confirmation wins even if timing failed. */
    error = sysfileclosewatch(pool, work, cache, anchor, key, error ? deadline : now, background);
    if (error != -ERR_WOULD_BLOCK)
      return error;
    error = systimerwait(deadline, EVENT_RQ_DONE, mask);
    if (error == -ERR_INTERRUPTED)
      return sysfilecloseinterrupt(pool, work, cache, anchor, key, background);
    if (error)
      return sysfileclosewatch(pool, work, cache, anchor, key, deadline, background);
  }
}

static isize
closework(Fileimage *state, Conntag tag)
{
  Conncache *cache = sysfileimagecache(state);
  Connentry  entry;
  Connwork  *work;
  u64        file;
  isize      error = sysconnview(cache, tag, &entry);
  if (error)
    return error;
  work = &state->connections[tag.index];
  if (work->state.tag.index != tag.index || work->state.tag.generation != tag.generation)
    return -ERR_STALE;
  if (work->state.operations[6].phase)
    file = work->state.operations[6].file;
  else {
    error = sysfilerefnextclose(&state->files, state->files.self, tag, 0, &file);
    if (error)
      return error;
  }
  return sysfileclosepump(&state->files, work, cache, file, 0);
}

/* After retiring a CLOSE, try the next one before waiting for another event.
   Keep pinned or in-flight Files discoverable for the next safe point. */
static void
closeprogress(Fileimage *state)
{
  const Fileref *ref;
  Connwork      *work;
  u32            i, pending = 0;
  if (!closing)
    return;
  for (i = 0; i < SYSFILE_REFS; i++) {
    ref = &state->files.entries[i];
    if (ref->state != SYSFILE_REF_CLOSING || ref->connection.index >= SYSCONN_COUNT)
      continue;
    work = &state->connections[ref->connection.index];
    if (work->state.tag.generation == ref->connection.generation)
      pending |= 1U << ref->connection.index;
  }
  closing = pending;
  for (i = 0; pending; i++, pending >>= 1)
    if (pending & 1) {
      Conntag tag = state->connections[i].state.tag;
      if (!closework(state, tag))
        (void) closework(state, tag);
    }
}

static int
fileprogress(Fileimage *state)
{
  Conncache *cache = sysfileimagecache(state);
  u32        i, slot, active = 0;
  for (i = 0; i < SYSCONN_COUNT && draining; i++) {
    Connwork *work = &state->connections[i];
    u32       bit = 1U << i;
    if (!(draining & bit))
      continue;
    draining &= ~bit;
    for (slot = 0; slot < FS_CONN_SLOTS; slot++) {
      Slotop *op = &work->state.operations[slot];
      Opkey   key;
      u64     file = 0;
      if ((op->flags & (SYSOP_BACKGROUND | SYSOP_APP_ENDED)) !=
              (SYSOP_BACKGROUND | SYSOP_APP_ENDED) ||
          (!ioopcode(op->opcode) && op->opcode != FS_POLL_QUERY && op->opcode != FS_POLL_WAIT &&
           op->opcode != FS_OPEN && op->opcode != FS_PIPE))
        continue;
      key.connection = work->state.tag;
      key.lease = op->lease;
      key.owner = 0;
      if (op->opcode == FS_OPEN || op->opcode == FS_PIPE) {
        if (sysfilecreatordrain(&state->files, work, cache, &key)) {
          draining |= bit;
          if (op->rpc || work->state.bootstrap.rpc)
            active = 1;
        } else
          closing |= bit;
      } else if (sysopdrain(work, cache, &key, &file)) {
        draining |= bit;
        if (op->rpc)
          active = 1;
      } else if (file)
        (void) sysfilerefunpin(&state->files, work->state.self, file);
    }
  }
  closeprogress(state);
  return active;
}

static int
fileevents(void)
{
  Conncache *cache = sysfileimagecache(&image);
  u32        i;
  if (fileprogress(&image))
    return 1;
  for (i = 0; i < SYSCONN_COUNT && closing; i++) {
    Slotop *op = &image.connections[i].state.operations[6];
    u32     state = cache->entries[i].state;
    if ((state == SYSCONN_READY || state == SYSCONN_DRAINING) && op->rpc &&
        op->opcode == FS_CLOSE && (op->flags & SYSOP_BACKGROUND))
      return 1;
  }
  return 0;
}

static isize
closefile(Fileimage *state, u64 file, u64 mask)
{
  Fileclose  anchor;
  Opkey      key, background = {0};
  Fileref    ref;
  Conncache *cache = sysfileimagecache(state);
  Connentry  entry;
  Connwork  *work;
  u64        now, deadline;
  isize      error;
  closing = 1;
  if (sysfilerefview(&state->files, state->files.self, file, &ref) ||
      sysconnview(cache, ref.connection, &entry) || systimernow(&now) ||
      now > UINT64_MAX - UINT64_C(1000000000))
    return -ERR_STATE;
  work = &state->connections[ref.connection.index];
  deadline = now + UINT64_C(1000000000);
  for (;;) {
    error = sysfileclosesubmit(&state->files, work, cache, file, deadline, &anchor, &key);
    if (!error)
      break;
    if (error != -ERR_BUSY && error != -ERR_CAPACITY)
      return -ERR_STATE;
    if (work->state.operations[6].phase && !closework(state, ref.connection))
      continue;
    if (systimernow(&now) || now >= deadline)
      return -ERR_STATE;
    error = systimerwait(deadline, EVENT_RQ_DONE, mask);
    if (error)
      return error == -ERR_INTERRUPTED ? error : -ERR_STATE;
  }
  error = sysfileclosewait(&state->files, work, cache, &anchor, &key, &background, mask);
  if (error == SYSFILE_CLOSE_SIGNAL)
    return -ERR_INTERRUPTED;
  return !error || error == SYSFILE_CLOSE_CONFIRMED ? 0 : -ERR_STATE;
}

isize
sysfileimageclose(int fd)
{
  Fileimage *state;
  Fdview     view;
  u64        mask, ignored, file = 0;
  isize      error, restored;
  if (fd < 0 || (u32) fd >= BOOT_FD_COUNT)
    return -ERR_BAD_HANDLE;
  error = sysfileimage(&state);
  if (error)
    return error;
  error = __sys_sigmask(SIGNAL_BLOCK, SIGNAL_SUPPORTED, &mask);
  if (error)
    return error;
  fileprogress(state);
  error = sysfdobserve(&state->fds, (u32) fd, &view);
  if (!error)
    error = view.tag.state == SYSFD_LIVE
                ? sysfdclose(&state->fds, &state->files, state->files.self, view.tag, &file)
                : -ERR_BAD_HANDLE;
  if (!error && file)
    error = closefile(state, file, mask);
  restored = __sys_sigmask(SIGNAL_SETMASK, mask, &ignored);
  return error ? error : restored;
}

static isize
duplicate(int fd, int target, u32 replace, u64 flags)
{
  Fileimage *state;
  u64        mask, ignored, displaced = 0;
  isize      result, restored;
  if (fd < 0 || (u32) fd >= BOOT_FD_COUNT ||
      (replace && (target < 0 || (u32) target >= BOOT_FD_COUNT)))
    return -ERR_BAD_HANDLE;
  if (!replace && (target < 0 || (u32) target >= BOOT_FD_COUNT))
    return -ERR_INVALID;
  result = sysfileimage(&state);
  if (result)
    return result;
  result = __sys_sigmask(SIGNAL_BLOCK, SIGNAL_SUPPORTED, &mask);
  if (result)
    return result;
  if (replace) {
    result = sysfddup2(&state->fds, &state->files, state->files.self, (u32) fd, (u32) target,
                       &displaced);
  } else
    result = sysfddup(&state->fds, &state->files, state->files.self, (u32) fd, (u32) target, flags);
  if (result >= 0 && displaced)
    closing = 1;
  fileprogress(state);
  restored = __sys_sigmask(SIGNAL_SETMASK, mask, &ignored);
  return restored ? restored : result;
}

isize
sysfileimagedup(int fd)
{
  return duplicate(fd, 0, 0, 0);
}

isize
sysfileimagedup2(int fd, int target)
{
  return duplicate(fd, target, 1, 0);
}

isize
sysfileimagedupfrom(int fd, int minimum, u64 flags)
{
  return duplicate(fd, minimum, 0, flags);
}

isize
sysfileimageflags(int fd, u32 set, u64 flags)
{
  Fileimage *state;
  Fdview     view;
  u64        mask, ignored;
  isize      result, restored;
  if (fd < 0 || (u32) fd >= BOOT_FD_COUNT)
    return -ERR_BAD_HANDLE;
  result = sysfileimage(&state);
  if (result)
    return result;
  result = __sys_sigmask(SIGNAL_BLOCK, SIGNAL_SUPPORTED, &mask);
  if (result)
    return result;
  result = sysfdobserve(&state->fds, (u32) fd, &view);
  if (!result) {
    if (view.tag.state != SYSFD_LIVE)
      result = -ERR_BAD_HANDLE;
    else if (set)
      result = sysfdsetflags(&state->fds, view.tag, flags);
    else
      result = (isize) view.flags;
  }
  restored = __sys_sigmask(SIGNAL_SETMASK, mask, &ignored);
  return restored ? restored : result;
}
