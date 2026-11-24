#include <time.h>
#include <errno.h>
#include <unistd.h>
#include "private/syscall.h"
#include "private/timer.h"

static int
sleeprelative(const struct timespec *request, struct timespec *remaining)
{
  const u64       second = UINT64_C(1000000000);
  struct timespec value;
  u64             nanos;
  isize           error;
  error = __sys_copy(&value, request, sizeof(value));
  if (error)
    return EFAULT;
  if (value.tv_sec < 0 || value.tv_nsec < 0 || (u64) value.tv_nsec >= second)
    return EINVAL;
  if ((u64) value.tv_sec > (UINT64_MAX - (u64) value.tv_nsec) / second)
    return EOVERFLOW;
  error = sysnanosleep((u64) value.tv_sec * second + (u64) value.tv_nsec, &nanos);
  if (!error)
    return 0;
  if (error == -ERR_INTERRUPTED && remaining) {
    value.tv_sec = (time_t) (nanos / second);
    value.tv_nsec = (long) (nanos % second);
    if (__sys_copy(remaining, &value, sizeof(value)))
      error = -ERR_FAULT;
  }
  return error == -ERR_INTERRUPTED ? EINTR
         : error == -ERR_FAULT     ? EFAULT
         : error == -ERR_OVERFLOW  ? EOVERFLOW
         : error == -ERR_NOTSUP    ? ENOTSUP
                                   : EIO;
}

int
nanosleep(const struct timespec *request, struct timespec *remaining)
{
  int error = sleeprelative(request, remaining);
  if (!error)
    return 0;
  errno = error;
  return -1;
}

#ifdef MIVINCI_MUSL
int
clock_nanosleep(clockid_t clock, int flags, const struct timespec *request,
                struct timespec *remaining)
{
  if ((clock != CLOCK_MONOTONIC && clock != CLOCK_REALTIME) || (flags & ~TIMER_ABSTIME))
    return EINVAL;
  if (flags)
    return ENOTSUP;
  return sleeprelative(request, remaining);
}

__attribute__((alias("clock_nanosleep"), visibility("hidden"))) int
__clock_nanosleep(clockid_t, int, const struct timespec *, struct timespec *);
#endif

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
