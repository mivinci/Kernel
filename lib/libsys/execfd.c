#include "private/execfd.h"

static int
separate(const void *a, u64 bytes, const void *b, u64 length)
{
  u64 x = (u64) (uintptr_t) a, y = (u64) (uintptr_t) b;
  return a && b && bytes <= UINT64_MAX - x && length <= UINT64_MAX - y &&
         (x + bytes <= y || y + length <= x);
}

isize
sysexecfdrelease(Execfds *capture)
{
  isize error;
  if (!capture || capture->count > BOOT_FD_COUNT || (!capture->pool && capture->count))
    return -ERR_INVALID;
  capture->ready = 0;
  while (capture->count) {
    error =
        sysfilerefunpin(capture->pool, capture->self, capture->entries[capture->count - 1].file);
    if (error)
      return error;
    capture->count--;
  }
  capture->pool = NULL;
  return 0;
}

isize
sysexecfds(Execfds *out, const Fdtable *fds, Filepool *pool, Conncache *connections, Connpeer self)
{
  const Fdentry *fd;
  Execfd        *entry;
  Fileref        file;
  Connentry      connection;
  isize          error, cleanup;
  u32            i;
  if (!separate(out, sizeof(*out), fds, sizeof(*fds)) ||
      !separate(out, sizeof(*out), pool, sizeof(*pool)) ||
      !separate(out, sizeof(*out), connections, sizeof(*connections)))
    return -ERR_INVALID;
  if (out->pool || out->count || out->ready)
    return -ERR_BUSY;
  error = sysfdcheck(fds);
  if (!error)
    error = sysconncheck(connections, self);
  if (!error)
    error = sysfilerefcheck(pool, self);
  if (error)
    return error;
  out->pool = pool;
  out->self = self;
  for (i = 0; i < BOOT_FD_COUNT; i++) {
    fd = &fds->entries[i];
    if (fd->state != SYSFD_LIVE)
      continue;
    error = sysfilerefview(pool, self, fd->file, &file);
    if (!error)
      error = sysconnview(connections, file.connection, &connection);
    if (!error && connection.state == SYSCONN_DRAINING)
      error = -ERR_PEER;
    if (!error)
      error = sysfilerefpin(pool, self, fd->file);
    if (error) {
      cleanup = sysexecfdrelease(out);
      return cleanup ? cleanup : error;
    }
    entry = &out->entries[out->count++];
    entry->backend = connection.peer;
    entry->file = fd->file;
    entry->session = file.session;
    entry->reference = file.reference;
    entry->fd = i;
    entry->flags = (u32) fd->flags;
  }
  out->ready = 1;
  return 0;
}
