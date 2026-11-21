#include <errno.h>
#include <unistd.h>
#include "private/fileimage.h"

int
dup(int fd)
{
  isize result = sysfileimagedup(fd);
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
  default:
    errno = EIO;
    break;
  }
  return -1;
}
