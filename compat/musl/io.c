#include <errno.h>
#include <unistd.h>
#include <abi/base.h>

extern long sysfilereadposix(unsigned int, int, void *, unsigned long);
extern long sysfiledup2posix(int, int);
extern long sysfilepipeposix(unsigned int, int *);
extern long sysfileimageclose(int);
extern unsigned int __libc_sigdepth(void);
extern long sysfilewritepairposix(unsigned int, int, const void *, unsigned long, const void *,
                                 unsigned long);

static ssize_t
posix(long result)
{
  if (result < 0) {
    errno = (int) -result;
    return -1;
  }
  return result;
}

ssize_t
read(int fd, void *buffer, size_t bytes)
{
  return posix(sysfilereadposix(__libc_sigdepth(), fd, buffer, bytes));
}

ssize_t
__mivinci_stdio_write(int fd, const void *first, size_t firstbytes, const void *second,
                      size_t secondbytes)
{
  return posix(sysfilewritepairposix(__libc_sigdepth(), fd, first, firstbytes, second, secondbytes));
}

ssize_t
write(int fd, const void *buffer, size_t bytes)
{
  return __mivinci_stdio_write(fd, buffer, bytes, 0, 0);
}

int
dup2(int oldfd, int newfd)
{
  return (int) posix(sysfiledup2posix(oldfd, newfd));
}

int
pipe(int fds[2])
{
  return (int) posix(sysfilepipeposix(__libc_sigdepth(), fds));
}

int
close(int fd)
{
  long status = sysfileimageclose(fd);
  if (!status)
    return 0;
  errno = status == -ERR_BAD_HANDLE ? EBADF : status == -ERR_INTERRUPTED ? EINPROGRESS : EIO;
  return -1;
}
