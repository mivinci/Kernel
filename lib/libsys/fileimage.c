#include "private/fileimage.h"
#include "private/close.h"
#include "private/sigentry.h"
#include "private/start.h"
#include "private/syscall.h"
#include "private/timer.h"
#include <abi/signal.h>
#include <proto/file.h>

static Fileimage image;
static Conntag   fstag;
static Rkey      hello;
static Bootmap   candidate;
static u32       initialized, connecting, closing;
static void      closeprogress(Fileimage *state);
static int       closeevents(void);

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
      systimerbindrq(closeevents);
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
  closeprogress(state);
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

/* At most one nonblocking step per backend at each library safe point.
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
    if (pending & 1)
      (void) closework(state, state->connections[i].state.tag);
}

static int
closeevents(void)
{
  Conncache *cache = sysfileimagecache(&image);
  u32        i;
  closeprogress(&image);
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
  closeprogress(state);
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
duplicate(int fd, int target, u32 replace)
{
  Fileimage *state;
  u64        mask, ignored, displaced = 0;
  isize      result, restored;
  if (fd < 0 || (u32) fd >= BOOT_FD_COUNT ||
      (replace && (target < 0 || (u32) target >= BOOT_FD_COUNT)))
    return -ERR_BAD_HANDLE;
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
    result = sysfddup(&state->fds, &state->files, state->files.self, (u32) fd, 0);
  if (result >= 0 && displaced)
    closing = 1;
  closeprogress(state);
  restored = __sys_sigmask(SIGNAL_SETMASK, mask, &ignored);
  return restored ? restored : result;
}

isize
sysfileimagedup(int fd)
{
  return duplicate(fd, 0, 0);
}

isize
sysfileimagedup2(int fd, int target)
{
  return duplicate(fd, target, 1);
}
