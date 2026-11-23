#include <unistd.h>
#include <errno.h>

long
sysconf(int name)
{
  if (name == _SC_PAGESIZE)
    return PAGE_SIZE;
  if (name == _SC_CLK_TCK)
    return 100;
  errno = EINVAL;
  return -1;
}
