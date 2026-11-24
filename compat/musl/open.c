#include <errno.h>
#include <fcntl.h>
#include <stdarg.h>
#include <abi/base.h>
#include <proto/file.h>

extern isize sysfileimageopenpath(u32, const char *, u64, u64, isize *);
extern u32 __libc_sigdepth(void);

int
open(const char *path, int flags, ...)
{
  const int supported = O_ACCMODE | O_CREAT | O_EXCL | O_TRUNC | O_APPEND | O_NONBLOCK | O_CLOEXEC;
  u64 mode = 0, native = flags & O_ACCMODE;
  isize status, business = 0;
  if ((flags & ~supported) || native == O_ACCMODE) {
    errno = EINVAL;
    return -1;
  }
  if (flags & O_CREAT) {
    va_list args;
    va_start(args, flags);
    mode = va_arg(args, mode_t);
    va_end(args);
    native |= FS_OPEN_CREAT;
  }
  native |= (flags & O_EXCL ? FS_OPEN_EXCL : 0) | (flags & O_TRUNC ? FS_OPEN_TRUNC : 0) |
            (flags & O_APPEND ? FS_OPEN_APPEND : 0) | (flags & O_NONBLOCK ? FS_OPEN_NONBLOCK : 0) |
            (flags & O_CLOEXEC ? FS_OPEN_CLOEXEC : 0);
  status = sysfileimageopenpath(__libc_sigdepth(), path, native, mode, &business);
  if (status >= 0)
    return (int) status;
  errno = business < 0 ? (int) -business : status == -ERR_NOTSUP ? ENOSYS :
          status == -ERR_CAPACITY ? EMFILE : status == -ERR_INTERRUPTED ? EINTR :
          status == -ERR_FAULT ? EFAULT : status == -ERR_INVALID ? EINVAL :
          status == -ERR_NOMEM ? ENOMEM : EIO;
  return -1;
}
