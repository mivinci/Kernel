#include <time.h>
#include <sys/time.h>
#include <sys/times.h>
#include <unistd.h>
#include <errno.h>
#include <abi/proc.h>
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

int
gettimeofday(struct timeval *value, void *timezone)
{
  struct timespec now;
  struct timeval  local;
  if (timezone) {
    errno = EINVAL;
    return -1;
  }
  if (clock_gettime(CLOCK_REALTIME, &now))
    return -1;
  local.tv_sec = now.tv_sec;
  local.tv_usec = now.tv_nsec / 1000;
  if (__sys_copy(value, &local, sizeof(local))) {
    errno = EFAULT;
    return -1;
  }
  return 0;
}

time_t
time(time_t *result)
{
  struct timespec now;
  if (clock_gettime(CLOCK_REALTIME, &now))
    return (time_t) -1;
  if (result && __sys_copy(result, &now.tv_sec, sizeof(now.tv_sec))) {
    errno = EFAULT;
    return (time_t) -1;
  }
  return now.tv_sec;
}

clock_t
times(struct tms *value)
{
  Queryrec   record;
  struct tms local;
  u64        written, next, ns_per_tick;
  isize      error;
  error = __sys_query(0, QUERY_TIMES, 0, &record, 1, &written, &next);
  if (error || written != 1 || next || record.type != QUERY_TIMES) {
    errno = error == -ERR_OVERFLOW ? EOVERFLOW : EIO;
    return -1;
  }
  ns_per_tick = UINT64_C(1000000000) / (u64) sysconf(_SC_CLK_TCK);
  local.tms_utime = (clock_t) (record.data[0] / ns_per_tick);
  local.tms_stime = (clock_t) (record.data[1] / ns_per_tick);
  local.tms_cutime = (clock_t) (record.data[2] / ns_per_tick);
  local.tms_cstime = (clock_t) (record.data[3] / ns_per_tick);
  if (value && __sys_copy(value, &local, sizeof(local))) {
    errno = EFAULT;
    return -1;
  }
  return (clock_t) (record.data[4] / ns_per_tick);
}
