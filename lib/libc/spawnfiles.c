#include "private/spawnfiles.h"
#include <abi/boot.h>
#include <errno.h>
#include <fcntl.h>
#include <stdlib.h>

#define SPAWN_PATH_BYTES 4096

static int
fdvalid(int fd)
{
  return fd >= 0 && (unsigned int) fd < BOOT_FD_COUNT;
}

static int
append(posix_spawn_file_actions_t *list, int kind, int fd, int target, const char *path, int flags,
       mode_t mode)
{
  Spawnaction *action;
  size_t       bytes = 0, i;
  int          saved = errno;
  if (!list)
    return EINVAL;
  if (!fdvalid(fd) || (kind == SPAWNFILE_DUP2 && !fdvalid(target)))
    return EBADF;
  if (kind == SPAWNFILE_OPEN) {
    if (!path)
      return EINVAL;
    while (bytes < SPAWN_PATH_BYTES && path[bytes])
      bytes++;
    if (bytes == SPAWN_PATH_BYTES)
      return ENAMETOOLONG;
    bytes++;
  }
  if (list->__count >= SPAWNFILES_LIMIT)
    return E2BIG;
  action = malloc(sizeof(*action) + bytes);
  errno = saved;
  if (!action)
    return ENOMEM;
  action->kind = kind;
  action->fd = fd;
  action->target = target;
  action->flags = flags;
  action->mode = mode;
  action->next = NULL;
  action->path = NULL;
  if (bytes) {
    char *copy = (char *) (action + 1);
    for (i = 0; i < bytes; i++)
      copy[i] = path[i];
    action->path = copy;
  }
  if (list->__last)
    list->__last->next = action;
  else
    list->__first = action;
  list->__last = action;
  list->__count++;
  return 0;
}

int
posix_spawn_file_actions_init(posix_spawn_file_actions_t *list)
{
  if (!list)
    return EINVAL;
  list->__first = list->__last = NULL;
  list->__count = 0;
  return 0;
}

int
posix_spawn_file_actions_destroy(posix_spawn_file_actions_t *list)
{
  Spawnaction *action, *next;
  int          saved = errno;
  if (!list)
    return EINVAL;
  for (action = list->__first; action; action = next) {
    next = action->next;
    free(action);
  }
  errno = saved;
  return posix_spawn_file_actions_init(list);
}

int
posix_spawn_file_actions_addopen(posix_spawn_file_actions_t *list, int fd, const char *path,
                                 int flags, mode_t mode)
{
  return append(list, SPAWNFILE_OPEN, fd, 0, path, flags, mode);
}

int
posix_spawn_file_actions_addclose(posix_spawn_file_actions_t *list, int fd)
{
  return append(list, SPAWNFILE_CLOSE, fd, 0, NULL, 0, 0);
}

int
posix_spawn_file_actions_adddup2(posix_spawn_file_actions_t *list, int fd, int target)
{
  return append(list, SPAWNFILE_DUP2, fd, target, NULL, 0, 0);
}

int
spawnfilesapply(const posix_spawn_file_actions_t *list, int (*apply)(void *, const Spawnaction *),
                void                             *child)
{
  const Spawnaction *action;
  int                error;
  if (!apply)
    return EINVAL;
  if (!list)
    return 0;
  for (action = list->__first; action; action = action->next) {
    error = apply(child, action);
    if (error)
      return error;
  }
  return 0;
}

typedef struct Spawnchild Spawnchild;
struct Spawnchild
{
  Fdtable  *fds;
  Filepool *pool;
  Connpeer  self;
  int (*openfile)(void *, const char *, int, mode_t, u64 *);
  void *context;
};

static int
fderror(isize error)
{
  if (error >= 0)
    return 0;
  if (error == -ERR_BAD_HANDLE)
    return EBADF;
  if (error == -ERR_CAPACITY)
    return EMFILE;
  if (error == -ERR_OVERFLOW)
    return EOVERFLOW;
  return EIO;
}

static isize
dropfd(Spawnchild *child, const Fdview *fd)
{
  u64 closing;
  if (fd->tag.state != SYSFD_LIVE)
    return fd->tag.state == SYSFD_RESERVED ? -ERR_STATE : 0;
  return sysfdclose(child->fds, child->pool, child->self, fd->tag, &closing);
}

static int
childaction(void *context, const Spawnaction *action)
{
  Spawnchild *child = context;
  Fdview      fd, old;
  u64         closing, file = 0;
  isize       status;
  int         error;
  status = sysfdobserve(child->fds, (u32) action->fd, &fd);
  if (status)
    return fderror(status);
  switch (action->kind) {
  case SPAWNFILE_DUP2:
    status = sysfddup2(child->fds, child->pool, child->self, (u32) action->fd, (u32) action->target,
                       &closing);
    if (status >= 0 && action->fd == action->target)
      status = sysfdsetflags(child->fds, fd.tag, 0);
    return fderror(status);
  case SPAWNFILE_CLOSE:
    return fderror(dropfd(child, &fd));
  case SPAWNFILE_OPEN:
    if (!child->openfile)
      return ENOSYS;
    status = dropfd(child, &fd);
    if (!status)
      status = sysfdobserve(child->fds, (u32) action->fd, &fd);
    if (status || fd.tag.state != SYSFD_FREE)
      return status ? fderror(status) : EMFILE;
    error = child->openfile(child->context, action->path, action->flags, action->mode, &file);
    if (error)
      return error;
    status =
        sysfdreplace(child->fds, fd.tag, file, action->flags & O_CLOEXEC ? FD_CLOEXEC : 0, &old);
    if (status && sysfilerefdrop(child->pool, child->self, file))
      return EIO;
    return fderror(status);
  default:
    return EINVAL;
  }
}

int
spawnfilesprepare(const posix_spawn_file_actions_t *list, Fdtable *fds, Filepool *pool,
                  Connpeer self, int (*openfile)(void *, const char *, int, mode_t, u64 *),
                  void    *context)
{
  Spawnchild child = {fds, pool, self, openfile, context};
  Fdview     fd;
  u32        i;
  isize      status = sysfdcheck(fds);
  int        error;
  if (!status)
    status = sysfilerefcheck(pool, self);
  if (status)
    return fderror(status);
  error = spawnfilesapply(list, childaction, &child);
  if (error)
    return error;
  for (i = 0; i < BOOT_FD_COUNT; i++) {
    status = sysfdobserve(fds, i, &fd);
    if (!status && fd.tag.state == SYSFD_LIVE && fd.flags & FD_CLOEXEC)
      status = dropfd(&child, &fd);
    if (status)
      return fderror(status);
  }
  return 0;
}
