#include <errno.h>
#include <unistd.h>

extern long sysfilereadposix(unsigned int, int, void *, unsigned long);
extern long sysfiledup2posix(int, int);
extern long sysfilepipeposix(unsigned int, int *);
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
  return posix(sysfilereadposix(0, fd, buffer, bytes));
}

ssize_t
__mivinci_stdio_write(int fd, const void *first, size_t firstbytes, const void *second,
                      size_t secondbytes)
{
  return posix(sysfilewritepairposix(0, fd, first, firstbytes, second, secondbytes));
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
  return (int) posix(sysfilepipeposix(0, fds));
}
