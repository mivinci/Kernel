#include <errno.h>
#include <fcntl.h>
#include <signal.h>
#include <stdarg.h>
#include <unistd.h>
#include "private/fileimage.h"

static int
dupresult(isize result)
{
  if (result >= 0)
    return (int) result;
  switch (result) {
  case -ERR_BAD_HANDLE:
    errno = EBADF;
    break;
  case -ERR_CAPACITY:
    errno = EMFILE;
    break;
  case -ERR_NOMEM:
    errno = ENOMEM;
    break;
  case -ERR_BUSY:
    errno = EBUSY;
    break;
  case -ERR_INVALID:
    errno = EINVAL;
    break;
  default:
    errno = EIO;
    break;
  }
  return -1;
}

int
dup(int fd)
{
  return dupresult(sysfileimagedup(fd));
}

int
dup2(int fd, int target)
{
  return dupresult(sysfileimagedup2(fd, target));
}

int
fcntl(int fd, int cmd, ...)
{
  va_list args;
  int     value;
  switch (cmd) {
  case F_GETFD:
    return dupresult(sysfileimageflags(fd, 0, 0));
  case F_DUPFD:
  case F_SETFD:
    va_start(args, cmd);
    value = va_arg(args, int);
    va_end(args);
    if (cmd == F_DUPFD)
      return dupresult(sysfileimagedupfrom(fd, value));
    return dupresult(sysfileimageflags(fd, 1, (u64) value & FD_CLOEXEC));
  default:
    errno = EINVAL;
    return -1;
  }
}

int
close(int fd)
{
  isize result = sysfileimageclose(fd);
  if (!result)
    return 0;
  errno = result == -ERR_BAD_HANDLE ? EBADF : result == -ERR_INTERRUPTED ? EINPROGRESS : EIO;
  return -1;
}

u32 __libc_sigdepth(void);

static ssize_t
transfer(int fd, u64 opcode, const void *input, void *output, size_t count)
{
  Fileioresult completed;
  isize status = sysfileimageio(__libc_sigdepth(), fd, opcode, input, output, count, &completed);
  int   error;
  if (!status) {
    status = completed.native;
    if (!status) {
      if (!completed.business)
        return (ssize_t) completed.count;
      error = completed.business >= -INT32_MAX ? (int) -completed.business : EIO;
      if (opcode == FS_WRITE && error == EPIPE)
        (void) raise(SIGPIPE);
      errno = error;
      return -1;
    }
  }
  switch (status) {
  case -ERR_BAD_HANDLE:
    error = EBADF;
    break;
  case -ERR_FAULT:
    error = EFAULT;
    break;
  case -ERR_CANCELLED:
    error = EINTR;
    break;
  case -ERR_BUSY:
  case -ERR_CAPACITY:
  case -ERR_LIMIT:
    error = EAGAIN;
    break;
  case -ERR_NOMEM:
    error = ENOMEM;
    break;
  default:
    error = EIO;
    break;
  }
  errno = error;
  return -1;
}

ssize_t
read(int fd, void *buffer, size_t count)
{
  return transfer(fd, FS_READ, NULL, buffer, count);
}

ssize_t
write(int fd, const void *buffer, size_t count)
{
  return transfer(fd, FS_WRITE, buffer, NULL, count);
}
