#include <time.h>
#include <errno.h>
#include "private/syscall.h"

static int
clockread(clockid_t clock, struct timespec *value, int resolution)
{
  struct timespec local;
  int64_t         seconds;
  u64             nanos;
  isize           error;
  error = resolution ? __sys_clock_getres((u64) clock, &seconds, &nanos)
                     : __sys_clock_gettime((u64) clock, &seconds, &nanos);
  if (error) {
    errno = error == -ERR_INVALID                         ? EINVAL
            : error == -ERR_OVERFLOW                      ? EOVERFLOW
            : error == -ERR_NOTSUP || error == -ERR_NOSYS ? ENOSYS
                                                          : EIO;
    return -1;
  }
  if (resolution && !value)
    return 0;
  if (nanos >= UINT64_C(1000000000)) {
    errno = EIO;
    return -1;
  }
  local.tv_sec = seconds;
  local.tv_nsec = (long) nanos;
  error = __sys_copy(value, &local, sizeof(local));
  if (error) {
    errno = error == -ERR_FAULT || error == -ERR_INVALID ? EFAULT : EIO;
    return -1;
  }
  return 0;
}

int
clock_gettime(clockid_t clock, struct timespec *value)
{
  return clockread(clock, value, 0);
}

int
clock_getres(clockid_t clock, struct timespec *value)
{
  return clockread(clock, value, 1);
}
