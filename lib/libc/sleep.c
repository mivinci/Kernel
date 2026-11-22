#include <time.h>
#include <errno.h>
#include <unistd.h>
#include "private/syscall.h"
#include "private/timer.h"

int
nanosleep(const struct timespec *request, struct timespec *remaining)
{
  const u64       second = UINT64_C(1000000000);
  struct timespec value;
  u64             nanos;
  isize           error;
  error = __sys_copy(&value, request, sizeof(value));
  if (error) {
    errno = EFAULT;
    return -1;
  }
  if (value.tv_sec < 0 || value.tv_nsec < 0 || (u64) value.tv_nsec >= second) {
    errno = EINVAL;
    return -1;
  }
  if ((u64) value.tv_sec > (UINT64_MAX - (u64) value.tv_nsec) / second) {
    errno = EOVERFLOW;
    return -1;
  }
  error = sysnanosleep((u64) value.tv_sec * second + (u64) value.tv_nsec, &nanos);
  if (!error)
    return 0;
  if (error == -ERR_INTERRUPTED && remaining) {
    value.tv_sec = (time_t) (nanos / second);
    value.tv_nsec = (long) (nanos % second);
    if (__sys_copy(remaining, &value, sizeof(value)))
      error = -ERR_FAULT;
  }
  errno = error == -ERR_INTERRUPTED ? EINTR
          : error == -ERR_FAULT     ? EFAULT
          : error == -ERR_OVERFLOW  ? EOVERFLOW
          : error == -ERR_NOTSUP    ? ENOTSUP
                                    : EIO;
  return -1;
}

unsigned int
sleep(unsigned int seconds)
{
  struct timespec request, remaining;
  int             saved = errno;
  if (!seconds)
    return 0;
  request.tv_sec = remaining.tv_sec = (time_t) seconds;
  request.tv_nsec = remaining.tv_nsec = 0;
  if (!nanosleep(&request, &remaining))
    seconds = 0;
  else
    seconds = (unsigned int) remaining.tv_sec + (remaining.tv_nsec != 0);
  errno = saved;
  return seconds;
}
