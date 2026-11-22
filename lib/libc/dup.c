#include <errno.h>
#include <fcntl.h>
#include <signal.h>
#include <stdarg.h>
#include <termios.h>
#include <unistd.h>
#include <proto/tty.h>
#include "private/fileimage.h"
#include "sigdepth.h"

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

static ssize_t
ioresult(u64 opcode, isize status, const Fileioresult *completed)
{
  int error;
  if (!status) {
    status = completed->native;
    if (!status) {
      if (!completed->business)
        return (ssize_t) completed->count;
      error = completed->business >= -INT32_MAX ? (int) -completed->business : EIO;
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
  case -ERR_INVALID:
    error = EINVAL;
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

int
open(const char *path, int flags, ...)
{
  Fileioresult completed = {0};
  va_list      args;
  u32          mode = 0;
  isize        status;
  if (flags & O_CREAT) {
    va_start(args, flags);
    mode = va_arg(args, unsigned int);
    va_end(args);
  }
  status = sysfileimageopenpath(__libc_sigdepth(), path, (u32) flags, mode, &completed.business);
  if (status >= 0)
    completed.count = (u64) status;
  else if (status == -ERR_CAPACITY)
    completed.business = -EMFILE;
  else if (status == -ERR_NOTSUP)
    completed.business = -ENOSYS;
  else if (status == -ERR_INTERRUPTED)
    completed.business = -EINTR;
  else
    return (int) ioresult(FS_OPEN, status, &completed);
  return (int) ioresult(FS_OPEN, 0, &completed);
}

static ssize_t
transfer(int fd, u64 opcode, const void *input, void *output, size_t count, off_t offset,
         u32 positional)
{
  Fileioresult completed;
  u32          layer = __libc_sigdepth();
  isize        status =
      positional ? sysfileimageposio(layer, fd, opcode, input, output, count, offset, &completed)
                        : sysfileimageio(layer, fd, opcode, input, output, count, &completed);
  return ioresult(opcode, status, &completed);
}

ssize_t
read(int fd, void *buffer, size_t count)
{
  return transfer(fd, FS_READ, NULL, buffer, count, 0, 0);
}

ssize_t
write(int fd, const void *buffer, size_t count)
{
  return transfer(fd, FS_WRITE, buffer, NULL, count, 0, 0);
}

ssize_t
pread(int fd, void *buffer, size_t count, off_t offset)
{
  return transfer(fd, FS_READ, NULL, buffer, count, offset, 1);
}

ssize_t
pwrite(int fd, const void *buffer, size_t count, off_t offset)
{
  return transfer(fd, FS_WRITE, buffer, NULL, count, offset, 1);
}

off_t
lseek(int fd, off_t offset, int whence)
{
  Fileioresult completed;
  isize        status = sysfileimageseek(__libc_sigdepth(), fd, offset, whence, &completed);
  return ioresult(FS_SEEK, status, &completed);
}

int
pipe(int fds[2])
{
  Fileioresult completed = {0};
  isize        status = sysfileimagepipepair(__libc_sigdepth(), fds, &completed.business);
  if (status == -ERR_CAPACITY)
    completed.business = -EMFILE;
  else if (status == -ERR_NOTSUP)
    completed.business = -ENOSYS;
  else
    return (int) ioresult(FS_PIPE, status, &completed);
  return (int) ioresult(FS_PIPE, 0, &completed);
}

static int
syncfd(int fd, u32 dataonly)
{
  Fileioresult completed;
  isize        status = sysfileimagesync(__libc_sigdepth(), fd, dataonly, &completed);
  return (int) ioresult(FS_SYNC, status, &completed);
}

int
fsync(int fd)
{
  return syncfd(fd, 0);
}

int
fdatasync(int fd)
{
  return syncfd(fd, 1);
}

int
chdir(const char *path)
{
  Fileioresult completed;
  isize        status = sysfileimagechdir(__libc_sigdepth(), path, &completed);
  if (status == -ERR_NOTSUP) {
    errno = ENOSYS;
    return -1;
  }
  return (int) ioresult(FS_PATH, status, &completed);
}

STATIC_ASSERT(sizeof(struct termios) == sizeof(Termarg), termios_size);
STATIC_ASSERT(offsetof(struct termios, c_iflag) == offsetof(Termarg, iflag), termios_iflag);
STATIC_ASSERT(offsetof(struct termios, c_oflag) == offsetof(Termarg, oflag), termios_oflag);
STATIC_ASSERT(offsetof(struct termios, c_cflag) == offsetof(Termarg, cflag), termios_cflag);
STATIC_ASSERT(offsetof(struct termios, c_lflag) == offsetof(Termarg, lflag), termios_lflag);
STATIC_ASSERT(offsetof(struct termios, c_ispeed) == offsetof(Termarg, ispeed), termios_ispeed);
STATIC_ASSERT(offsetof(struct termios, c_ospeed) == offsetof(Termarg, ospeed), termios_ospeed);
STATIC_ASSERT(offsetof(struct termios, c_cc) == offsetof(Termarg, cc), termios_cc);
STATIC_ASSERT(ICRNL == TTY_ICRNL && OPOST == TTY_OPOST && ONLCR == TTY_ONLCR && CS8 == TTY_CS8 &&
                  ICANON == TTY_ICANON && ECHO == TTY_ECHO && ISIG == TTY_ISIG,
              termios_flags);
STATIC_ASSERT(VINTR == TTY_VINTR && VQUIT == TTY_VQUIT && VERASE == TTY_VERASE &&
                  VKILL == TTY_VKILL && VEOF == TTY_VEOF && VTIME == TTY_VTIME &&
                  VMIN == TTY_VMIN && VSUSP == TTY_VSUSP,
              termios_cc_indices);

int
tcgetattr(int fd, struct termios *attributes)
{
  Fileioresult completed;
  isize        status = sysfileimagegetattr(__libc_sigdepth(), fd, attributes, &completed);
  return (int) ioresult(FS_TTYCTL, status, &completed);
}

int
isatty(int fd)
{
  struct termios attributes;
  return tcgetattr(fd, &attributes) == 0;
}
