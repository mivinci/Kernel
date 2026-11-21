#include <errno.h>
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
close(int fd)
{
  isize result = sysfileimageclose(fd);
  if (!result)
    return 0;
  errno = result == -ERR_BAD_HANDLE ? EBADF : result == -ERR_INTERRUPTED ? EINPROGRESS : EIO;
  return -1;
}
