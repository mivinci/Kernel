#include <unistd.h>
#include <errno.h>

long
sysconf(int name)
{
  if (name == _SC_PAGESIZE)
    return PAGE_SIZE;
  errno = EINVAL;
  return -1;
}
