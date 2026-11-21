#include "private/fileimage.h"
#include "private/close.h"
#include "private/sigentry.h"
#include "private/start.h"
#include "private/syscall.h"
#include <abi/signal.h>
#include <proto/file.h>

static Fileimage image;
static Conntag   fstag;
static Rkey      hello;
static Bootmap   candidate;
static u32       initialized, connecting;

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

isize
sysfileimageclosework(Conntag tag)
{
  Fileimage *state;
  u64        mask, ignored;
  isize      result, restored;
  result = sysfileimage(&state);
  if (result)
    return result;
  result = __sys_sigmask(SIGNAL_BLOCK, SIGNAL_SUPPORTED, &mask);
  if (result)
    return result;
  result = closework(state, tag);
  restored = __sys_sigmask(SIGNAL_SETMASK, mask, &ignored);
  return restored ? restored : result;
}

static isize
duplicate(int fd, int target, u32 replace)
{
  Fileimage *state;
  Fileref    old;
  u64        mask, ignored, closing = 0;
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
    result =
        sysfddup2(&state->fds, &state->files, state->files.self, (u32) fd, (u32) target, &closing);
    /* The replacement has committed. The old reference owns any remaining
       close work, regardless of whether a control request can advance now. */
    if (result >= 0 && closing && !sysfilerefview(&state->files, state->files.self, closing, &old))
      (void) closework(state, old.connection);
  } else
    result = sysfddup(&state->fds, &state->files, state->files.self, (u32) fd, 0);
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
