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

static int
samepeer(Connpeer a, Connpeer b)
{
  return a.pid == b.pid && a.image == b.image;
}

isize
sysexecfdmanifest(const Execfds *capture, Connpeer backend, u64 target_image, u64 deadline,
                  u64 args[8], Fileinherit *rows, u64 capacity)
{
  const Execfd *fd;
  u64           bytes, i, count = 0;
  if (!capture || !capture->pool || capture->ready != 1 || capture->count > BOOT_FD_COUNT)
    return -ERR_STATE;
  if (handlekind(backend.pid) != HANDLE_PROCESS || !backend.image ||
      target_image <= capture->self.image || !deadline || capacity > BOOT_FD_COUNT ||
      !separate(args, 8 * sizeof(*args), capture, sizeof(*capture)) ||
      !separate(args, 8 * sizeof(*args), capture->pool, sizeof(*capture->pool)))
    return -ERR_INVALID;
  bytes = capacity * sizeof(*rows);
  if (capacity && (!separate(rows, bytes, capture, sizeof(*capture)) ||
                   !separate(rows, bytes, capture->pool, sizeof(*capture->pool)) ||
                   !separate(rows, bytes, args, 8 * sizeof(*args))))
    return -ERR_INVALID;
  for (i = 0; i < capture->count; i++)
    if (samepeer(capture->entries[i].backend, backend))
      count++;
  if (count > capacity)
    return -ERR_CAPACITY;
  count = 0;
  for (i = 0; i < capture->count; i++) {
    fd = &capture->entries[i];
    if (!samepeer(fd->backend, backend))
      continue;
    rows[count].fd = fd->fd;
    rows[count].flags = fd->flags;
    rows[count].backend_pid = (u64) backend.pid;
    rows[count].backend_image = backend.image;
    rows[count].session = fd->session;
    rows[count].reference_id = fd->reference;
    count++;
  }
  args[0] = (u64) capture->self.pid;
  args[1] = target_image;
  args[2] = capture->self.image;
  args[3] = deadline;
  args[4] = count;
  args[5] = args[6] = args[7] = 0;
  return (isize) count;
}
