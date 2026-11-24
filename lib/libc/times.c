#include <sys/times.h>
#include <errno.h>
#include "private/syscall.h"

clock_t
times(struct tms *value)
{
  Queryrec   record;
  struct tms local;
  u64        written, next;
  const u64  ns_per_tick = 10000000;
  isize      error = __sys_query(0, QUERY_TIMES, 0, &record, 1, &written, &next);
  if (error || written != 1 || next || record.type != QUERY_TIMES) {
    errno = error == -ERR_OVERFLOW ? EOVERFLOW : EIO;
    return -1;
  }
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
